#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#define CATCH_BENCHMARK_REPORTER_IMPLEMENTATION
#include "tests/catch_benchmark_reporter.hpp"

#include "benchmark_corpus.h"
#include "mixed_content_scenarios.h"

#include "ctextencodingdetector.h"
#include "ctextparser.h"
#include "trigramfrequencytables/ctrigramfrequencytable_french.h"
#include "trigramfrequencytables/ctrigramfrequencytable_german.h"
#include "trigramfrequencytables/ctrigramfrequencytable_polish.h"
#include "trigramfrequencytables/ctrigramfrequencytable_russian.h"
#include "trigramfrequencytables/ctrigramfrequencytable_spanish.h"

#include <hash/wheathash.hpp>

DISABLE_COMPILER_WARNINGS
#include <QTextCodec>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

// What decode() costs, and where the cost goes. Every case is tagged [!benchmark], which Catch2 treats as
// hidden: the binary with no arguments runs the tests only.
//   text-encoding-detector-tests "[!benchmark]" -r fastest --benchmark-no-analysis
//
// decode() takes one of four routes, and the bytes of the input alone decide which:
//   a byte order mark - named outright
//   a NUL byte anywhere - BOM-less UTF-16/32 by the phase of its NULs, else declined as binary before any probe runs
//   valid UTF-8, pure ASCII included - decodeUtf8() answers and detect() never runs
//   anything else - every codec in the shortlist decodes a bounded sample of the input, and each decoding is parsed and
//                   scored; the winner then decodes the whole input once
// The last route is the one detect()'s own numbers describe, and a binary file without a NUL byte takes it too.
//
// detect() is not instrumented. Its total is measured, and the per-codec work it repeats is measured beside it
// through the same public calls, so the phases can be weighed against the total without touching the library.
//
// The corpus is Cyrillic, so its UTF-8 form runs two bytes to the character. An ASCII file of the same
// character count is the cheaper input of the two, and these numbers bound it from above.

namespace {

// Russian, the language whose encodings this library can actually tell apart
constexpr auto corpusLanguage = BenchmarkCorpus::Language::Russian;

// A log ladder up to the corpus length: these numbers exist to place a size threshold, and a linear ladder
// would spend most of its points in the range that is already too slow to choose.
constexpr qsizetype characterCounts[] = { 4 * 1024, 16 * 1024, 64 * 1024, 256 * 1024, 768 * 1024 };

// Representative of the whole shortlist: one Cyrillic 8-bit codec, and the single-byte decode loop is the same for all of them
constexpr const char* eightBitCodecName = "Windows-1251";

// detect() is linear in the input with a large constant, and Catch2 runs a case many times over: past this
// length a single case costs minutes.
constexpr qsizetype maxCharactersForDetect = 256 * 1024;

[[nodiscard]] std::string sizeLabel(qsizetype characters, qsizetype bytes)
{
	return std::to_string(characters / 1024) + "K chars, " + std::to_string(bytes / 1024) + " KB";
}

[[nodiscard]] std::string caseName(const char* what, const char* codecName, qsizetype characters, qsizetype bytes)
{
	return std::string{ what } + ", " + codecName + ", " + sizeLabel(characters, bytes);
}

// The corpus is committed, so this only ever fires if the ladder outgrows it
[[nodiscard]] bool skipped(qsizetype characters)
{
	return BenchmarkCorpus::text(corpusLanguage).size() < characters;
}

}

TEST_CASE("decode(): the whole call", "[!benchmark]")
{
	// UTF-8 takes the fast route, the 8-bit codec the slow one, and UTF-16LE the wide one: a NUL count over the
	// whole input, the decode, and a pass over the result to confirm it reads as text.
	for (const char* codecName : { "UTF-8", eightBitCodecName, "UTF-16LE" })
	{
		for (const qsizetype characters : characterCounts)
		{
			if (skipped(characters))
				continue;

			const QByteArray data = BenchmarkCorpus::encoded(corpusLanguage, codecName, characters);
			BENCHMARK(caseName("decode", codecName, characters, data.size()))
			{
				return CTextEncodingDetector::decode(data).text.size();
			};
		}
	}
}

TEST_CASE("decode(): the UTF-8 shortcut alone", "[!benchmark]")
{
	// decodeUtf8() is the whole of the fast route; what decode() adds is the DecodedText around it
	for (const qsizetype characters : characterCounts)
	{
		if (skipped(characters))
			continue;

		const QByteArray data = BenchmarkCorpus::encoded(corpusLanguage, "UTF-8", characters);
		BENCHMARK(caseName("decodeUtf8", "UTF-8", characters, data.size()))
		{
			const auto text = decodeUtf8(data);
			return text ? text->size() : 0;
		};
	}
}

TEST_CASE("detect(): the whole call", "[!benchmark]")
{
	for (const qsizetype characters : characterCounts)
	{
		if (characters > maxCharactersForDetect || skipped(characters))
			continue;

		const QByteArray data = BenchmarkCorpus::encoded(corpusLanguage, eightBitCodecName, characters);
		BENCHMARK(caseName("detect", eightBitCodecName, characters, data.size()))
		{
			return CTextEncodingDetector::detect(data).size();
		};
	}
}

TEST_CASE("detect(): the work it repeats per codec", "[!benchmark]")
{
	// detect() runs all three of these once per surviving codec, of which there are a dozen
	for (const qsizetype characters : characterCounts)
	{
		if (characters > maxCharactersForDetect || skipped(characters))
			continue;

		const QByteArray data = BenchmarkCorpus::encoded(corpusLanguage, eightBitCodecName, characters);
		QTextCodec* const codec = QTextCodec::codecForName(eightBitCodecName);
		REQUIRE(codec);

		BENCHMARK(caseName("per codec: decode", eightBitCodecName, characters, data.size()))
		{
			const std::unique_ptr<QTextDecoder> decoder{ codec->makeDecoder() };
			return decoder->toUnicode(data).size();
		};

		const QString decodedText = [codec, &data] {
			const std::unique_ptr<QTextDecoder> decoder{ codec->makeDecoder() };
			return decoder->toUnicode(data);
		}();

		BENCHMARK(caseName("per codec: dedup hash", eightBitCodecName, characters, data.size()))
		{
			return ::wheathash64(decodedText.constData(), decodedText.size() * sizeof(QChar));
		};

		BENCHMARK(caseName("per codec: parse", eightBitCodecName, characters, data.size()))
		{
			CTextParser parser;
			parser.parse(decodedText);
			return parser.parsingResult().totalTrigramsCount;
		};
	}
}

// parse() is the largest phase of a detection pass, and its per-character cost depends on the script: only a
// non-ASCII character reaches QChar's Unicode tables. Detection meets the whole range, from Cyrillic prose that is
// barely ASCII to the ASCII host of a mixed file.
TEST_CASE("parse(): by script", "[!benchmark]")
{
	// The shortest of the three test texts bounds the length they can be compared at
	constexpr qsizetype characters = 128 * 1024;

	const struct { const char* script; QString text; } cases[] = {
		{ "Cyrillic prose", BenchmarkCorpus::slice(BenchmarkCorpus::text(BenchmarkCorpus::Language::Russian), characters) },
		{ "Latin prose", BenchmarkCorpus::slice(BenchmarkCorpus::text(BenchmarkCorpus::Language::French), characters) },
		{ "ASCII host", BenchmarkCorpus::slice(BenchmarkCorpus::text(BenchmarkCorpus::Host::Json), characters) }
	};

	for (const auto& testCase : cases)
	{
		BENCHMARK(std::string{ "parse, " } + testCase.script + ", " + std::to_string(testCase.text.size() / 1024) + "K chars")
		{
			CTextParser parser;
			parser.parse(testCase.text);
			return parser.parsingResult().totalTrigramsCount;
		};
	}
}

// The same range end to end, on the slow route every time. A file mixed into an ASCII host is the detector's
// most ASCII input that still reaches detection at all: one without a non-ASCII byte is valid UTF-8, and
// decodeUtf8() answers it.
TEST_CASE("decode(): by script", "[!benchmark]")
{
	constexpr qsizetype characters = 128 * 1024;

	const std::vector<MixedContent::Scenario> scenarios = MixedContent::scenarios();
	const auto scenarioText = [&scenarios](const char* scenarioName) {
		const auto scenario = std::ranges::find(scenarios, scenarioName, &MixedContent::Scenario::name);
		REQUIRE(scenario != scenarios.end());
		return scenario->text;
	};

	const struct { const char* input; QString text; const char* codecName; } cases[] = {
		{ "1% Cyrillic in a JSON host", scenarioText("russian 1% json clustered"), "Windows-1251" },
		{ "5% Cyrillic in C source", scenarioText("russian 5% code interleaved"), "Windows-1251" },
		{ "Latin prose", BenchmarkCorpus::text(BenchmarkCorpus::Language::French), "ISO-8859-1" },
		{ "Cyrillic prose", BenchmarkCorpus::text(BenchmarkCorpus::Language::Russian), "Windows-1251" }
	};

	for (const auto& testCase : cases)
	{
		const QByteArray data = BenchmarkCorpus::encoded(BenchmarkCorpus::slice(testCase.text, characters), testCase.codecName);

		// A slice that kept no non-ASCII byte would take the UTF-8 route and measure nothing
		REQUIRE(std::ranges::any_of(data, [](char byte) { return static_cast<unsigned char>(byte) >= 0x80; }));

		BENCHMARK(std::string{ "decode, " } + testCase.input + ", " + std::to_string(data.size() / 1024) + " KB")
		{
			return CTextEncodingDetector::decode(data).text.size();
		};
	}
}

TEST_CASE("detect(): the frequency tables it builds once", "[!benchmark]")
{
	// The tables are built on the first detect() call and kept: this is what that call pays over the rest
	BENCHMARK("all five frequency tables, constructed")
	{
		const CTrigramFrequencyTable_French french;
		const CTrigramFrequencyTable_German german;
		const CTrigramFrequencyTable_Polish polish;
		const CTrigramFrequencyTable_Russian russian;
		const CTrigramFrequencyTable_Spanish spanish;
		return french.trigramOccurrenceTable().totalTrigramsCount + german.trigramOccurrenceTable().totalTrigramsCount
			+ polish.trigramOccurrenceTable().totalTrigramsCount + russian.trigramOccurrenceTable().totalTrigramsCount
			+ spanish.trigramOccurrenceTable().totalTrigramsCount;
	};
}

TEST_CASE("decode(): binary input", "[!benchmark]")
{
	// An executable image has NUL bytes in its first page, so this is the binary guard's cost and nothing else.
	// A binary file without a NUL byte is never valid UTF-8 and takes the slow route, at the Windows-1251 rate
	// above or worse: garbage yields more distinct trigrams than language does.
	for (const qsizetype bytes : { 64 * 1024, 256 * 1024, 1024 * 1024 })
	{
		const QByteArray data = BenchmarkCorpus::executableBytes(bytes);
		if (data.size() < bytes)
			continue;

		BENCHMARK("decode, executable image, " + std::to_string(bytes / 1024) + " KB")
		{
			return CTextEncodingDetector::decode(data).text.size();
		};
	}
}
