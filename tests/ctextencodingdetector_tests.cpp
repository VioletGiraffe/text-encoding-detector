#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "benchmark_corpus.h"
#include "mixed_content_scenarios.h"

#include "ctextencodingdetector.h"

#include <algorithm>
#include <string>
#include <vector>

// Two things are under test: the corpus is what it claims to be, and decode() does not lie about it.
// A corpus file that cannot survive its own codecs would leave every benchmark and every study measuring
// text with replacement characters in it, and nothing downstream would say so.

namespace {

constexpr qsizetype sampleCharacters = 64 * 1024;
// Under decode()'s 0.95 cut-off, so that a drift toward it fails before it becomes a decline
constexpr double scoreCap = 0.90;

// Catch2 renders a QString as a list of its characters, which for a text this size buries the run.
// The comparison reports where the two part instead, and shows only that neighbourhood.
[[nodiscard]] qsizetype firstDifference(const QString& left, const QString& right)
{
	const qsizetype common = std::min(left.size(), right.size());
	for (qsizetype i = 0; i < common; ++i)
	{
		if (left[i] != right[i])
			return i;
	}

	return left.size() == right.size() ? -1 : common;
}

[[nodiscard]] std::string around(const QString& text, qsizetype position)
{
	return text.mid(qMax(qsizetype{ 0 }, position - 16), 32).toStdString();
}

// 'expected' written as 'codecName' must come back from decode() unchanged, and detect() must rank that
// reading first with its score under the cap
void checkRecovered(const std::string& label, const QString& expected, const char* codecName)
{
	const QByteArray data = BenchmarkCorpus::encoded(expected, codecName);
	REQUIRE(!data.isEmpty());

	const auto results = CTextEncodingDetector::detect(data);
	REQUIRE(!results.empty());

	const QString decoded = CTextEncodingDetector::decode(data).text;
	const qsizetype difference = firstDifference(decoded, expected);

	INFO(label << " written as " << codecName << ", read as " << results.front().encoding.toStdString() << " "
		<< results.front().language.toStdString() << " at " << results.front().score << "; decoded " << decoded.size()
		<< " of " << expected.size() << " characters, first difference at " << difference
		<< ": [" << around(decoded, difference) << "] against [" << around(expected, difference) << "]");

	CHECK(results.front().score < scoreCap);
	CHECK(difference == -1);
}

// A corpus text cut to the size of one detection, less a trailing lone surrogate
[[nodiscard]] QString sample(const QString& text)
{
	return BenchmarkCorpus::slice(text, sampleCharacters);
}

}

TEST_CASE("Every corpus file is present and holds text")
{
	constexpr qsizetype shortest = 20000; // The Polish test prefix is the shortest, at 24,000

	for (const BenchmarkCorpus::Language language : BenchmarkCorpus::allLanguages)
	{
		INFO("corpus/test/" << BenchmarkCorpus::name(language) << ".txt");
		REQUIRE(BenchmarkCorpus::text(language).size() > shortest);
	}

	for (const BenchmarkCorpus::OtherAuthor author : BenchmarkCorpus::allOtherAuthors)
	{
		INFO("corpus/test/" << BenchmarkCorpus::name(BenchmarkCorpus::language(author)) << "-" << BenchmarkCorpus::name(author) << ".txt");
		REQUIRE(BenchmarkCorpus::text(author).size() > shortest);
	}

	for (const BenchmarkCorpus::Host host : { BenchmarkCorpus::Host::Code, BenchmarkCorpus::Host::Json })
	{
		INFO("corpus/test/" << BenchmarkCorpus::name(host) << ".txt");
		REQUIRE(BenchmarkCorpus::text(host).size() >= MixedContent::scenarioCharacters);
	}
}

TEST_CASE("Each corpus text survives the codecs it claims, character for character")
{
	// The contract prepare_corpus.ps1 enforces when it writes the files. A character a codec cannot carry
	// encodes as '?', which is an ASCII byte: it would both distort the trigram statistics and act as ASCII
	// filler inside text that is supposed to be non-ASCII.
	const auto check = [](const char* name, const QString& expected, const std::vector<const char*>& codecNames) {
		for (const char* codecName : codecNames)
		{
			const QString carried = BenchmarkCorpus::representable(expected, codecName);
			const qsizetype difference = firstDifference(carried, expected);

			INFO(name << " as " << codecName << ", first difference at " << difference
				<< ": [" << around(carried, difference) << "] against [" << around(expected, difference) << "]");

			CHECK(difference == -1);
		}
	};

	for (const BenchmarkCorpus::Language language : BenchmarkCorpus::allLanguages)
		check(BenchmarkCorpus::name(language), sample(BenchmarkCorpus::text(language)), BenchmarkCorpus::codecNames(language));

	for (const BenchmarkCorpus::OtherAuthor author : BenchmarkCorpus::allOtherAuthors)
		check(BenchmarkCorpus::name(author), sample(BenchmarkCorpus::text(author)), BenchmarkCorpus::codecNames(BenchmarkCorpus::language(author)));
}

TEST_CASE("decode() recovers each language's prose from every encoding it is written in")
{
	for (const BenchmarkCorpus::Language language : BenchmarkCorpus::nonAsciiLanguages)
	{
		for (const char* codecName : BenchmarkCorpus::codecNames(language))
			checkRecovered(BenchmarkCorpus::name(language), sample(BenchmarkCorpus::text(language)), codecName);
	}
}

TEST_CASE("decode() recovers prose by an author the language's table was not built from")
{
	for (const BenchmarkCorpus::OtherAuthor author : BenchmarkCorpus::allOtherAuthors)
	{
		for (const char* codecName : BenchmarkCorpus::codecNames(BenchmarkCorpus::language(author)))
			checkRecovered(BenchmarkCorpus::name(author), sample(BenchmarkCorpus::text(author)), codecName);
	}
}

// Whole files: the size a file viewer hands over, and the sizes and shapes the window study is calibrated on
TEST_CASE("decode() recovers each language from every host, shape and share it is mixed in at")
{
	for (const MixedContent::Scenario& scenario : MixedContent::scenarios())
	{
		for (const char* codecName : scenario.codecNames)
			checkRecovered(scenario.name, scenario.text, codecName);
	}
}
