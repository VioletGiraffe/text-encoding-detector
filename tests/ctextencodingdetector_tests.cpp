#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils
RESTORE_COMPILER_WARNINGS

#include "benchmark_corpus.h"
#include "mixed_content_scenarios.h"

#include "ctextencodingdetector.h"

DISABLE_COMPILER_WARNINGS
#include <QTextCodec>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <cstdint>
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

// 'expected' written as 'codecName' must come back from decode() unchanged, with the reading's score under the cap
void checkRecovered(const std::string& label, const QString& expected, const char* codecName)
{
	const QByteArray data = BenchmarkCorpus::encoded(expected, codecName);
	REQUIRE(!data.isEmpty());

	const auto answer = CTextEncodingDetector::decode(data);
	const qsizetype difference = firstDifference(answer.text, expected);

	INFO(label << " written as " << codecName << ", read as " << answer.encoding.toStdString() << " " << answer.language.toStdString()
		<< " at " << answer.score << "; decoded " << answer.text.size() << " of " << expected.size() << " characters, first difference at "
		<< difference << ": [" << around(answer.text, difference) << "] against [" << around(expected, difference) << "]");

	CHECK(answer.score < scoreCap);
	CHECK(difference == -1);
}

// A corpus text cut to the size of one detection, less a trailing lone surrogate
[[nodiscard]] QString sample(const QString& text)
{
	return BenchmarkCorpus::slice(text, sampleCharacters);
}

// Little-endian 16-bit integers under 256: exactly UTF-16LE's NUL layout, decoding to Latin-1 and controls
[[nodiscard]] QByteArray littleEndianUint16Ramp()
{
	QByteArray ramp;
	for (int i = 0; i < 32768; ++i)
		ramp.append(static_cast<char>(i & 0xFF)).append('\0');
	return ramp;
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

// detect() drops a name Qt cannot resolve without a word, so a misspelt entry would silently narrow the shortlist
TEST_CASE("Every codec in detect()'s shortlist resolves")
{
	for (const char* codecName : CTextEncodingDetector::codecShortlist())
	{
		INFO(codecName);
		CHECK(QTextCodec::codecForName(codecName) != nullptr);
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

// Past the budget the sample chooses the codec and the whole file is what the choice has to be right about.
// The sparsest scenarios, 1% prose, and the densest, pure prose, each grown to twice the budget by repetition.
TEST_CASE("decode() recovers files past the detection sample budget")
{
	for (const MixedContent::Scenario& scenario : MixedContent::scenarios())
	{
		if (!scenario.name.ends_with(" prose") && scenario.name.find(" 1% ") == std::string::npos)
			continue;

		QString grown;
		while (grown.size() < 2 * CTextEncodingDetector::detectionSampleBudget) // 8-bit codecs: one byte per character
			grown += scenario.text;

		for (const char* codecName : scenario.codecNames)
			checkRecovered(scenario.name + ", grown past the budget", grown, codecName);
	}
}

// Every corpus text, the ASCII hosts included, in each wide encoding with and without a byte order mark: the
// mark or the layout of the NUL bytes names the encoding, the text is read back exactly and reported as certain
TEST_CASE("decode() reads UTF-16 and UTF-32, with or without a byte order mark")
{
	struct WideCodec { const char* name; const char* bom; int bomSize; };
	constexpr WideCodec wideCodecs[] = {
		{ "UTF-16LE", "\xFF\xFE", 2 }, { "UTF-16BE", "\xFE\xFF", 2 }, { "UTF-32LE", "\xFF\xFE\x00\x00", 4 }, { "UTF-32BE", "\x00\x00\xFE\xFF", 4 }
	};

	const auto check = [&](const char* name, const QString& expected) {
		for (const WideCodec& codec : wideCodecs)
		{
			const QByteArray bare = BenchmarkCorpus::encoded(expected, codec.name);
			REQUIRE(!bare.isEmpty());

			for (const bool withBom : { false, true })
			{
				const QByteArray data = withBom ? QByteArray{ codec.bom, codec.bomSize } + bare : bare;
				const auto answer = CTextEncodingDetector::decode(data);
				const qsizetype difference = firstDifference(answer.text, expected);

				INFO(name << " written as " << codec.name << (withBom ? " with" : " without") << " a BOM, read as " << answer.encoding.toStdString()
					<< " at " << answer.score << "; first difference at " << difference << ": [" << around(answer.text, difference) << "] against ["
					<< around(expected, difference) << "]");

				CHECK(answer.encoding == QLatin1String{ codec.name });
				CHECK(answer.score == 0.0);
				CHECK(difference == -1);
			}
		}
	};

	for (const BenchmarkCorpus::Language language : BenchmarkCorpus::allLanguages)
		check(BenchmarkCorpus::name(language), sample(BenchmarkCorpus::text(language)));

	for (const BenchmarkCorpus::Host host : { BenchmarkCorpus::Host::Code, BenchmarkCorpus::Host::Json })
		check(BenchmarkCorpus::name(host), sample(BenchmarkCorpus::text(host)));
}

// What a NUL byte means outside those layouts, and what a wide layout of the wrong content means
TEST_CASE("decode() declines binary and NUL-carrying 8-bit text")
{
	const auto declines = [](const char* name, const QByteArray& data) {
		REQUIRE(!data.isEmpty());
		const auto answer = CTextEncodingDetector::decode(data);
		INFO(name << ": read as " << answer.encoding.toStdString() << " " << answer.language.toStdString() << " at " << answer.score
			<< ", " << answer.text.size() << " characters");
		CHECK(answer.encoding.isEmpty());
		CHECK(answer.text.isEmpty());
	};

	declines("the test executable", BenchmarkCorpus::executableBytes(sampleCharacters));

	// A NUL-terminated 8-bit file is binary by the rule every diff tool uses
	QByteArray terminated = BenchmarkCorpus::encoded(sample(BenchmarkCorpus::text(BenchmarkCorpus::Language::Russian)), "Windows-1251");
	terminated.append('\0');
	declines("Windows-1251 text with a trailing NUL", terminated);

	declines("a ramp of little-endian uint16 values", littleEndianUint16Ramp());

	// Quiet 16-bit audio: small values of either sign, high bytes 0x00 or 0xFF
	QByteArray audio;
	uint32_t seed = 20260910u;
	for (int i = 0; i < 32768; ++i)
	{
		seed = seed * 1664525u + 1013904223u;
		const auto value = static_cast<int16_t>(static_cast<int32_t>(seed >> 24) - 128);
		audio.append(static_cast<char>(value & 0xFF)).append(static_cast<char>((value >> 8) & 0xFF));
	}
	declines("quiet little-endian 16-bit audio", audio);
}

TEST_CASE("wideEncodingFromNulLayout() names the wide encoding by the NUL layout alone, text or not")
{
	const auto layoutEncoding = [](const QByteArray& data) {
		const char* const encoding = CTextEncodingDetector::wideEncodingFromNulLayout(data);
		return std::string{ encoding ? encoding : "" };
	};

	const QString text = sample(BenchmarkCorpus::text(BenchmarkCorpus::Host::Code));
	for (const char* codecName : { "UTF-16LE", "UTF-16BE", "UTF-32LE", "UTF-32BE" })
		CHECK(layoutEncoding(BenchmarkCorpus::encoded(text, codecName)) == codecName);

	CHECK(layoutEncoding(littleEndianUint16Ramp()) == "UTF-16LE");
	CHECK(layoutEncoding(BenchmarkCorpus::encoded(text, "UTF-8")).empty());
	CHECK(layoutEncoding(BenchmarkCorpus::executableBytes(sampleCharacters)).empty());
}
