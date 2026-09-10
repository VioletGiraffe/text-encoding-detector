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
#include "trigramfrequencytables/ctrigramfrequencytable_french.h"
#include "trigramfrequencytables/ctrigramfrequencytable_german.h"
#include "trigramfrequencytables/ctrigramfrequencytable_polish.h"
#include "trigramfrequencytables/ctrigramfrequencytable_russian.h"
#include "trigramfrequencytables/ctrigramfrequencytable_spanish.h"

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
// decode() takes one of three routes, and the bytes of the input alone decide which:
//   a NUL byte anywhere - declined as binary before any probe runs
//   valid UTF-8, pure ASCII included - isUtf8() answers and detect() never runs
//   anything else - every codec in the shortlist decodes the whole input, and each decoding is parsed and scored
// The third route is the one a threshold has to be set against, and a binary file without a NUL byte takes it in full.
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

// Only an 8-bit codec leaves decode() without a shortcut: UTF-16 text without a byte order mark is declined
// as binary on its NUL bytes before any probe runs.
[[nodiscard]] bool takesTheSlowRoute(const char* codecName)
{
	return std::string_view{ codecName } == eightBitCodecName;
}

}

TEST_CASE("decode(): the whole call", "[!benchmark]")
{
	// UTF-8 takes the fast route, the 8-bit codec the slow one, and UTF-16LE is declined as binary: its cost is
	// the byte scan up to the first NUL, which for Cyrillic text is the first space.
	for (const char* codecName : { "UTF-8", eightBitCodecName, "UTF-16LE" })
	{
		for (const qsizetype characters : characterCounts)
		{
			if (skipped(characters) || (characters > maxCharactersForDetect && takesTheSlowRoute(codecName)))
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
	// isUtf8() is the whole cost of the fast route, and decode() then converts the same bytes a second time
	for (const qsizetype characters : characterCounts)
	{
		if (skipped(characters))
			continue;

		const QByteArray data = BenchmarkCorpus::encoded(corpusLanguage, "UTF-8", characters);
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

TEST_CASE("detect(): the frequency tables it builds once", "[!benchmark]")
{
	// The tables are built on the first detect() call and kept: this is what that call pays over the rest
	BENCHMARK("all six frequency tables, constructed")
	{
		const CTrigramFrequencyTable_English english;
		const CTrigramFrequencyTable_French french;
		const CTrigramFrequencyTable_German german;
		const CTrigramFrequencyTable_Polish polish;
		const CTrigramFrequencyTable_Russian russian;
		const CTrigramFrequencyTable_Spanish spanish;
		return english.trigramOccurrenceTable().totalTrigramsCount + french.trigramOccurrenceTable().totalTrigramsCount
			+ german.trigramOccurrenceTable().totalTrigramsCount + polish.trigramOccurrenceTable().totalTrigramsCount
			+ russian.trigramOccurrenceTable().totalTrigramsCount + spanish.trigramOccurrenceTable().totalTrigramsCount;
	};
}

TEST_CASE("decode(): binary input", "[!benchmark]")
{
	// An executable image has NUL bytes in its first page, so this is the binary guard's cost and nothing else.
	// A binary file without a NUL byte is never valid UTF-8 and takes the slow route in full, at the Windows-1251
	// rate above or worse: garbage yields more distinct trigrams than language does.
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
