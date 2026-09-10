#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#define CATCH_BENCHMARK_REPORTER_IMPLEMENTATION
#include "tests/catch_benchmark_reporter.hpp"

#include "benchmark_corpus.h"

#include "ctextencodingdetector.h"
#include "ctextparser.h"
#include "trigramfrequencytables/ctrigramfrequencytable_english.h"
#include "trigramfrequencytables/ctrigramfrequencytable_russian.h"

#include <hash/wheathash.hpp>

DISABLE_COMPILER_WARNINGS
#include <QTextCodec>
RESTORE_COMPILER_WARNINGS

#include <memory>
#include <string>
#include <string_view>

// What decode() costs, and where the cost goes. Every case is tagged [!benchmark], which Catch2 treats as
// hidden: the binary with no arguments runs the tests only.
//   text-encoding-detector-tests "[!benchmark]" -r fastest --benchmark-no-analysis
//
// decode() takes one of two routes, and the encoding of the input alone decides which:
//   valid UTF-8, pure ASCII included - isUtf8() answers and detect() never runs
//   anything else - every codec in the shortlist decodes the whole input, and each decoding is parsed and scored
// The second route is the one a threshold has to be set against, and binary files always take it.
//
// detect() is not instrumented. Its total is measured, and the per-codec work it repeats is measured beside it
// through the same public calls, so the phases can be weighed against the total without touching the library.
//
// The corpus is Cyrillic, so its UTF-8 form runs two bytes to the character. An ASCII file of the same
// character count is the cheaper input of the two, and these numbers bound it from above.

namespace {

// A log ladder: these numbers exist to place a size threshold, and a linear ladder would spend most of its
// points in the range that is already too slow to choose.
constexpr qsizetype characterCounts[] = { 4 * 1024, 16 * 1024, 64 * 1024, 256 * 1024, 1024 * 1024, 4 * 1024 * 1024 };

// Representative of the whole shortlist: one Cyrillic 8-bit codec, and the single-byte decode loop is the same for all of them
constexpr const char* eightBitCodecName = "Windows-1251";

// detect() is linear in the input with a large constant, and Catch2 runs a case many times over: past this
// length a single case costs minutes.
constexpr qsizetype maxCharactersForDetect = 1024 * 1024;

[[nodiscard]] std::string sizeLabel(qsizetype characters, qsizetype bytes)
{
	return std::to_string(characters / 1024) + "K chars, " + std::to_string(bytes / 1024) + " KB";
}

[[nodiscard]] std::string caseName(const char* what, const char* codecName, qsizetype characters, qsizetype bytes)
{
	return std::string{ what } + ", " + codecName + ", " + sizeLabel(characters, bytes);
}

// True where the corpus is missing or too short for this length, having reported a missing corpus once
[[nodiscard]] bool skipped(qsizetype characters)
{
	if (!BenchmarkCorpus::text().isEmpty())
		return BenchmarkCorpus::text().size() < characters;

	static bool reported = false;
	if (!reported)
	{
		reported = true;
		WARN(BenchmarkCorpus::missingCorpusMessage());
	}

	return true;
}

// Every codec but UTF-8 leaves decode() with no shortcut to take, and detect() then runs in full
[[nodiscard]] bool takesTheSlowRoute(const char* codecName)
{
	return std::string_view{ codecName } != "UTF-8";
}

}

TEST_CASE("decode(): the whole call", "[!benchmark]")
{
	// UTF-8 and UTF-16LE stand for the fast route, the 8-bit codec for the slow one. UTF-16 without a BOM is
	// the worst realistic input: no shortcut takes it, and every 8-bit codec finds letters in it.
	for (const char* codecName : { "UTF-8", eightBitCodecName, "UTF-16LE" })
	{
		for (const qsizetype characters : characterCounts)
		{
			if (skipped(characters) || (characters > maxCharactersForDetect && takesTheSlowRoute(codecName)))
				continue;

			const QByteArray data = BenchmarkCorpus::encoded(codecName, characters);
			BENCHMARK(caseName("decode", codecName, characters, data.size()))
			{
				return CTextEncodingDetector::decode(data).text.size();
			};
		}
	}
}

TEST_CASE("decode(): the UTF-8 shortcut alone", "[!benchmark]")
{
	// isUtf8() is the whole cost of the fast route, and decode() then converts the same bytes a second time
	for (const qsizetype characters : characterCounts)
	{
		if (skipped(characters))
			continue;

		const QByteArray data = BenchmarkCorpus::encoded("UTF-8", characters);
		BENCHMARK(caseName("isUtf8", "UTF-8", characters, data.size()))
		{
			return isUtf8(data);
		};
	}
}

TEST_CASE("detect(): the whole call", "[!benchmark]")
{
	for (const qsizetype characters : characterCounts)
	{
		if (characters > maxCharactersForDetect || skipped(characters))
			continue;

		const QByteArray data = BenchmarkCorpus::encoded(eightBitCodecName, characters);
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

		const QByteArray data = BenchmarkCorpus::encoded(eightBitCodecName, characters);
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

TEST_CASE("detect(): the frequency tables it builds per call", "[!benchmark]")
{
	// Both tables are locals of detect(), rebuilt and thrown away on every call: a fixed cost no input can shrink
	BENCHMARK("both frequency tables, constructed")
	{
		const CTrigramFrequencyTable_English english;
		const CTrigramFrequencyTable_Russian russian;
		return english.trigramOccurrenceTable().totalTrigramsCount + russian.trigramOccurrenceTable().totalTrigramsCount;
	};
}

TEST_CASE("decode(): binary input", "[!benchmark]")
{
	// A binary file is never valid UTF-8, so it takes the slow route in full - the case a size threshold
	// exists to prevent, and the one no guard currently catches.
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
