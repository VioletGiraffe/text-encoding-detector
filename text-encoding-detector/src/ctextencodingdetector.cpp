#include "ctextencodingdetector.h"
#include "trigramfrequencytables/ctrigramfrequencytable_french.h"
#include "trigramfrequencytables/ctrigramfrequencytable_german.h"
#include "trigramfrequencytables/ctrigramfrequencytable_polish.h"
#include "trigramfrequencytables/ctrigramfrequencytable_russian.h"
#include "trigramfrequencytables/ctrigramfrequencytable_spanish.h"

#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QTextCodec>
RESTORE_COMPILER_WARNINGS

#include <array>
#include <algorithm>
#include <cmath>
#include <string.h> // memcmp
#include <memory>
#include <ranges>

// cosineDistance(): 0.0 is best, 1.0 means no useful trigram overlap.
static constexpr double plausibleMatchThreshold = 0.95;

[[nodiscard]] inline bool startsWithBytes(const QByteArray& data, const char* bytes, int bytesSize) noexcept
{
	return data.size() >= bytesSize && ::memcmp(data.constData(), bytes, bytesSize) == 0;
}

bool isBinary(const QByteArray& data)
{
	return data.contains('\0');
}

bool isUtf8(const QByteArray& data)
{
	const QString text = QString::fromUtf8(data);
	return text.toUtf8() == data;
}

[[nodiscard]] inline double logProbabilityScore(const CTextParser::OccurrenceTable& model, const CTextParser::OccurrenceTable& sample) noexcept
{
	static constexpr double Lmax = 15.0;
	if (sample.totalTrigramsCount <= 5) // Too little data to draw conclusions
		return 1e20;

	double totalLoss = 0.0;
	quint64 totalCount = 0;

	for (const auto& [trigram, stats] : sample.trigramOccurrenceTable)
	{
		const quint64 count = stats.rawCount;
		const auto it = model.trigramOccurrenceTable.find(trigram);

		const double loss =
			(it != model.trigramOccurrenceTable.end())
			? std::min(static_cast<double>(it->second.loss), Lmax)
			: Lmax;

		totalLoss += static_cast<double>(count) * loss;
		totalCount += count;
	}

	if (totalCount == 0)
		return 1e20; // arbitrary large number to represent no match

	return totalLoss / static_cast<double>(totalCount);
}

// Only the trigrams with a non-ASCII character count, on both sides: the ASCII ones decode the same under every
// codec and would only dilute. A reading that turns the non-ASCII into replacement characters keeps no trigram
// at all and scores 1.0; one that maps it to the wrong letters keeps trigrams the table has never seen.
[[nodiscard]] inline double cosineDistance(const CTrigramFrequencyTable_Base& model, const CTextParser::OccurrenceTable& sample) noexcept
{
	const double unknownPenaltyWeight = 0.0; // Adjust this weight to control the penalty for unknown trigrams
	const auto& modelTable = model.trigramOccurrenceTable().trigramOccurrenceTable;
	if (modelTable.empty() || sample.trigramOccurrenceTable.empty())
		return 1.0 + unknownPenaltyWeight;

	double dot = 0.0;
	double sampleNormSq = 0.0;
	double unknownCount = 0.0;
	double sampleTotalCount = 0.0;

	for (const auto& [trigram, sampleStats] : sample.trigramOccurrenceTable)
	{
		if (!trigram.hasNonAsciiCharacter())
			continue;

		const double sampleCount = static_cast<double>(sampleStats.rawCount);
		sampleNormSq += sampleCount * sampleCount;
		sampleTotalCount += sampleCount;

		const auto modelIt = modelTable.find(trigram);
		if (modelIt != modelTable.end())
		{
			const double modelCount = static_cast<double>(modelIt->second.rawCount);
			dot += sampleCount * modelCount;
		}
		else
		{
			unknownCount += sampleCount;
		}
	}

	const double modelNormSq = model.countsNormSquared();
	if (modelNormSq <= 0.0 || sampleNormSq <= 0.0 || sampleTotalCount <= 0.0)
		return 1.0 + unknownPenaltyWeight;

	const double similarity = dot / (std::sqrt(modelNormSq) * std::sqrt(sampleNormSq));
	const double clampedSimilarity = std::clamp(similarity, 0.0, 1.0);

	const double cosineDistance = 1.0 - clampedSimilarity;
	const double unknownFraction = unknownCount / sampleTotalCount;

	return cosineDistance + unknownPenaltyWeight * unknownFraction;
}

template <typename Container, typename Value>
inline bool contains(const Container& container, const Value& value)
{
	return std::ranges::find(container, value) != container.end();
}

// Read-only once built, so one instance serves every call from every thread.
// No English table: its trigrams would all be ASCII, which scoring ignores, and pure ASCII never reaches detect().
[[nodiscard]] static const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& defaultLanguageTables()
{
	static const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>> tables = [] {
		std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>> built;
		built.push_back(std::make_unique<CTrigramFrequencyTable_French>());
		built.push_back(std::make_unique<CTrigramFrequencyTable_German>());
		built.push_back(std::make_unique<CTrigramFrequencyTable_Polish>());
		built.push_back(std::make_unique<CTrigramFrequencyTable_Russian>());
		built.push_back(std::make_unique<CTrigramFrequencyTable_Spanish>());
		return built;
	}();

	return tables;
}

CTextEncodingDetector::DecodedText CTextEncodingDetector::decode(const QByteArray & textData, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages)
{
	// BOM-less UTF-16/32 is declined here too: it carries NUL bytes
	if (isBinary(textData))
		return {};

	if (auto decodedText = decodeUtfBom(textData); !decodedText.encoding.isEmpty())
		return decodedText;

	if (isUtf8(textData))
		return DecodedText{QString::fromUtf8(textData), "UTF-8", {}};

	const auto detectionResult = detect(textData, tablesForLanguages);
	if (!detectionResult.empty() && detectionResult.front().score < plausibleMatchThreshold)
	{
		QTextCodec * codec = QTextCodec::codecForName(detectionResult.front().encoding.toUtf8().data());
		assert_r(codec);
		if (codec)
			return DecodedText{codec->toUnicode(textData), detectionResult.front().encoding, detectionResult.front().language};
	}

	return DecodedText();
}

CTextEncodingDetector::DecodedText CTextEncodingDetector::decodeUtfBom(const QByteArray& textData)
{
	struct BomEncoding
	{
		const char* bytes;
		int bytesSize;
		const char* encoding;
	};

	static constexpr std::array bomEncodings {
		BomEncoding{ "\xFF\xFE\x00\x00", 4, "UTF-32LE" },
		BomEncoding{ "\x00\x00\xFE\xFF", 4, "UTF-32BE" },
		BomEncoding{ "\xEF\xBB\xBF", 3, "UTF-8" },
		BomEncoding{ "\xFF\xFE", 2, "UTF-16LE" },
		BomEncoding{ "\xFE\xFF", 2, "UTF-16BE" },
	};

	for (const auto& bomEncoding : bomEncodings)
	{
		if (!startsWithBytes(textData, bomEncoding.bytes, bomEncoding.bytesSize))
			continue;

		QTextCodec* codec = QTextCodec::codecForName(bomEncoding.encoding);
		assert_r(codec);
		if (!codec)
			return {};

		return DecodedText{
			codec->toUnicode(textData.constData() + bomEncoding.bytesSize, static_cast<int>(textData.size() - bomEncoding.bytesSize)),
			bomEncoding.encoding,
			{}
		};
	}

	return {};
}

std::vector<CTextEncodingDetector::EncodingDetectionResult> CTextEncodingDetector::detect(const QByteArray & textData, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages)
{
	std::array encodingsShortlist {
		"Windows-1251",
		"KOI8-R",
		"KOI8-U",
		"CP866",
		"ISO-8859-1",
		"ISO-8859-2",
		"UTF-16LE",
		"UTF-16BE",
		"UTF-32LE",
		"UTF-32BE",
		"UTF-8",
	};

	std::vector<QTextCodec*> codecs;
	codecs.reserve(encodingsShortlist.size() + 2);

	for (const char* encodingName : encodingsShortlist)
	{
		QTextCodec* codec = QTextCodec::codecForName(encodingName);
		if (codec && !contains(codecs, codec))
			codecs.push_back(codec);
	}

	if (auto* localeCodec = QTextCodec::codecForLocale(); localeCodec && !contains(codecs, localeCodec))
		codecs.push_back(localeCodec);

	if (auto* utfCodec = QTextCodec::codecForUtfText(textData, nullptr); utfCodec)
	{
		if (!contains(codecs, utfCodec))
			codecs.push_back(utfCodec);
	}

	const auto& languageStatisticsTables = tablesForLanguages.empty() ? defaultLanguageTables() : tablesForLanguages;

	std::vector<uint64_t> hashes;
	hashes.reserve(codecs.size());

	std::vector<CTextEncodingDetector::EncodingDetectionResult> match;
	match.reserve(codecs.size() * languageStatisticsTables.size());

	for (const auto& codec : codecs)
	{
		const std::unique_ptr<QTextDecoder> decoder{ codec->makeDecoder() };
		const QString decodedText = decoder->toUnicode(textData);

		// Skip duplicate codecs that produce the same decoded text
		const auto hash = ::wheathash64(decodedText.constData(), decodedText.size() * sizeof(QChar));
		if (contains(hashes, hash))
			continue;

		hashes.push_back(hash);

		CTextParser parser;
		if (!parser.parse(decodedText))
			continue;

		for (const auto& table : languageStatisticsTables)
		{
			const double distanceScore = cosineDistance(*table, parser.parsingResult());
			match.emplace_back(CTextEncodingDetector::EncodingDetectionResult{ codec->name(), table->language(), distanceScore });
		}
	}

	std::ranges::sort(match, std::less{}, &CTextEncodingDetector::EncodingDetectionResult::score);
	return match;
}
