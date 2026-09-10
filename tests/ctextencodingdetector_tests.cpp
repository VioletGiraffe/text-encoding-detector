#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "benchmark_corpus.h"

#include "ctextencodingdetector.h"

DISABLE_COMPILER_WARNINGS
#include <QFile>
RESTORE_COMPILER_WARNINGS

// These guard the benchmarks' premises. Read with the wrong codec, the corpus decodes to mojibake, and every
// number the benchmarks produce is then measured over a text no encoding could have produced.

namespace {

// True where the corpus is missing, having reported it
[[nodiscard]] bool corpusMissing()
{
	if (!BenchmarkCorpus::text().isEmpty())
		return false;

	WARN(BenchmarkCorpus::missingCorpusMessage());
	return true;
}

constexpr qsizetype sampleCharacters = 64 * 1024;

}

TEST_CASE("The corpus file holds the encoding the benchmarks read it with")
{
	if (corpusMissing())
		return;

	QFile file{ BenchmarkCorpus::filePath() };
	REQUIRE(file.open(QIODevice::ReadOnly));

	const auto results = CTextEncodingDetector::detect(file.read(64 * 1024)); // A prefix: detection needs no more, and the file runs to megabytes
	REQUIRE(!results.empty());
	CHECK(results.front().encoding.compare(QString::fromLatin1(BenchmarkCorpus::codecName()), Qt::CaseInsensitive) == 0);
}

TEST_CASE("decode() recovers the corpus text from every encoding the benchmarks feed it")
{
	if (corpusMissing())
		return;

	const QString expected = BenchmarkCorpus::slice(sampleCharacters);

	for (const char* codecName : { "KOI8-R", "Windows-1251", "CP866", "UTF-8", "UTF-16LE" })
	{
		const QByteArray data = BenchmarkCorpus::encoded(codecName, sampleCharacters);
		REQUIRE(!data.isEmpty());

		INFO("Encoded as " << codecName);
		CHECK(CTextEncodingDetector::decode(data).text == expected);
	}
}
