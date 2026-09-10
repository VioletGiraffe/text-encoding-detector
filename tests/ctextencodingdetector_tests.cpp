#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "benchmark_corpus.h"

#include "ctextencodingdetector.h"

#include <algorithm>
#include <string>

// Two things are under test: the corpus is what it claims to be, and decode() does not lie about it.
// A corpus file that cannot survive its own codecs would leave every benchmark and every study measuring
// text with replacement characters in it, and nothing downstream would say so.

namespace {

constexpr qsizetype sampleCharacters = 64 * 1024;

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

}

TEST_CASE("Every corpus file is present and holds text")
{
	for (const BenchmarkCorpus::Language language : BenchmarkCorpus::allLanguages)
	{
		INFO("corpus/" << BenchmarkCorpus::name(language) << ".txt");
		REQUIRE(BenchmarkCorpus::text(language).size() > 100000);
	}
}

TEST_CASE("Each corpus text survives the codecs it claims, character for character")
{
	// The contract prepare_corpus.ps1 enforces when it writes the files. A character a codec cannot carry
	// encodes as '?', which is an ASCII byte: it would both distort the trigram statistics and act as ASCII
	// filler inside text that is supposed to be non-ASCII.
	for (const BenchmarkCorpus::Language language : BenchmarkCorpus::allLanguages)
	{
		const QString expected = BenchmarkCorpus::slice(language, sampleCharacters);

		for (const char* codecName : BenchmarkCorpus::codecNames(language))
		{
			const QString carried = BenchmarkCorpus::representable(language, codecName, sampleCharacters);
			const qsizetype difference = firstDifference(carried, expected);

			INFO(BenchmarkCorpus::name(language) << " as " << codecName << ", first difference at " << difference
				<< ": [" << around(carried, difference) << "] against [" << around(expected, difference) << "]");

			CHECK(difference == -1);
		}
	}
}

TEST_CASE("decode() recovers Russian text from every encoding it is written in")
{
	const BenchmarkCorpus::Language russian = BenchmarkCorpus::Language::Russian;
	const QString expected = BenchmarkCorpus::slice(russian, sampleCharacters);

	for (const char* codecName : BenchmarkCorpus::codecNames(russian))
	{
		const QByteArray data = BenchmarkCorpus::encoded(russian, codecName, sampleCharacters);
		REQUIRE(!data.isEmpty());

		const QString decoded = CTextEncodingDetector::decode(data).text;
		const qsizetype difference = firstDifference(decoded, expected);

		INFO("Encoded as " << codecName << ", decoded " << decoded.size() << " of " << expected.size()
			<< " characters, first difference at " << difference
			<< ": [" << around(decoded, difference) << "] against [" << around(expected, difference) << "]");

		CHECK(difference == -1);
	}
}

// Known defect, and the tag says so: Catch2 fails this case if it ever starts passing, which is the point.
// Western European is not detectable with the tables this library carries - accented Latin text matches
// neither the English model, whose trigrams hold no accents, nor the Russian one. Declining would be a fine
// answer, and the caller would fall back. Instead decode() reads the file as UTF-8, which turns every accented
// byte into a replacement character: those are not letters, so parse() drops them and closes the surrounding
// letters over the gap, scoring better than the correct reading whose accented trigrams the model has never
// seen. The scoring rewards a codec for destroying what it cannot match, and the caller cannot tell.
TEST_CASE("decode() declines rather than guessing at Western European text", "[!shouldfail]")
{
	for (const BenchmarkCorpus::Language language :
		{ BenchmarkCorpus::Language::French, BenchmarkCorpus::Language::German,
		  BenchmarkCorpus::Language::Spanish, BenchmarkCorpus::Language::Polish })
	{
		for (const char* codecName : BenchmarkCorpus::codecNames(language))
		{
			const QString expected = BenchmarkCorpus::slice(language, sampleCharacters);
			const QByteArray data = BenchmarkCorpus::encoded(language, codecName, sampleCharacters);
			const auto answer = CTextEncodingDetector::decode(data);

			INFO(BenchmarkCorpus::name(language) << " written as " << codecName << ", read back as "
				<< (answer.encoding.isEmpty() ? std::string{ "nothing" } : answer.encoding.toStdString())
				<< " " << answer.language.toStdString());

			CHECK((answer.text.isEmpty() || answer.text == expected));
		}
	}
}
