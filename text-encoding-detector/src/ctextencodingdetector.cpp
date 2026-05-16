#include "ctextencodingdetector.h"
#include "trigramfrequencytables/ctrigramfrequencytable_english.h"
#include "trigramfrequencytables/ctrigramfrequencytable_russian.h"

#include "qtcore_helpers/qstring_helpers.hpp"

#include "assert/advanced_assert.h"
#include "lang/type_traits_fast.hpp"

DISABLE_COMPILER_WARNINGS
#include <QFile>
#include <QIODevice>
#include <QTextCodec>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <memory>
#include <ranges>
#include <unordered_set>

#include <math.h>

#include <algorithm>
#include <cmath>
#include <limits>

static constexpr double plausibleMatchThreshold = 20.0;

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

[[nodiscard]] inline double cosineDistance(const CTextParser::OccurrenceTable& model, const CTextParser::OccurrenceTable& sample) noexcept
{
	const double unknownPenaltyWeight = 0.0; // Adjust this weight to control the penalty for unknown trigrams
	if (model.trigramOccurrenceTable.empty() || sample.trigramOccurrenceTable.empty())
		return 1.0 + unknownPenaltyWeight;

	double dot = 0.0;
	double modelNormSq = 0.0;
	double sampleNormSq = 0.0;
	double unknownCount = 0.0;
	double sampleTotalCount = 0.0;

	for (const auto& [trigram, sampleStats] : sample.trigramOccurrenceTable)
	{
		const double sampleCount = static_cast<double>(sampleStats.rawCount);
		sampleNormSq += sampleCount * sampleCount;
		sampleTotalCount += sampleCount;

		const auto modelIt = model.trigramOccurrenceTable.find(trigram);
		if (modelIt != model.trigramOccurrenceTable.end())
		{
			const double modelCount = static_cast<double>(modelIt->second.rawCount);
			dot += sampleCount * modelCount;
		}
		else
		{
			unknownCount += sampleCount;
		}
	}

	for (const auto& [_, modelStats] : model.trigramOccurrenceTable)
	{
		const double modelCount = static_cast<double>(modelStats.rawCount);
		modelNormSq += modelCount * modelCount;
	}

	if (modelNormSq <= 0.0 || sampleNormSq <= 0.0 || sampleTotalCount <= 0.0)
		return 1.0 + unknownPenaltyWeight;

	const double similarity = dot / (std::sqrt(modelNormSq) * std::sqrt(sampleNormSq));
	const double clampedSimilarity = std::clamp(similarity, 0.0, 1.0);

	const double cosineDistance = 1.0 - clampedSimilarity;
	const double unknownFraction = unknownCount / sampleTotalCount;

	return cosineDistance + unknownPenaltyWeight * unknownFraction;
}

template <typename T>
std::vector<CTextEncodingDetector::EncodingDetectionResult> detect(T& dataOrInputDevice, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages)
{
	const auto availableCodecs = QTextCodec::availableCodecs();

	std::decay_t<decltype(tablesForLanguages)> defaultTables;
	if (tablesForLanguages.empty())
	{
		defaultTables.emplace_back(std::make_unique<CTrigramFrequencyTable_English>());
		defaultTables.emplace_back(std::make_unique<CTrigramFrequencyTable_Russian>());
	}

	std::unordered_set<QTextCodec*> differentCodecs;
	for (const auto& codecName : availableCodecs)
	{
		differentCodecs.insert(QTextCodec::codecForName(codecName.data()));
	}

	std::vector<CTextEncodingDetector::EncodingDetectionResult> match;
	for (const auto& codec: differentCodecs)
	{
		CTextParser parser;
		if (!parser.parse(dataOrInputDevice, QString(codec->name()), false, false))
			continue;

		const auto& languageStatisticsTables = tablesForLanguages.empty() ? defaultTables : tablesForLanguages;
		for (const auto& table: languageStatisticsTables)
			match.emplace_back(CTextEncodingDetector::EncodingDetectionResult{ codec->name(), table->language(), cosineDistance(table->trigramOccurrenceTable(), parser.parsingResult()) });
	}

	std::ranges::sort(match, std::less{}, &CTextEncodingDetector::EncodingDetectionResult::score);
	return match;
}


CTextEncodingDetector::DecodedText CTextEncodingDetector::decode(const QString & textFilePath, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages)
{
	const auto detectionResult = detect(textFilePath, tablesForLanguages);
	if (!detectionResult.empty() && detectionResult.front().score < plausibleMatchThreshold)
	{
		QTextCodec * codec = QTextCodec::codecForName(detectionResult.front().encoding.toUtf8().data());
		assert_r(codec);
		if (codec)
		{
			QFile file(textFilePath);
			file.open(QIODevice::ReadOnly);
			return DecodedText{codec->toUnicode(file.readAll()), detectionResult.front().encoding, detectionResult.front().language};
		}
	}

	return DecodedText();
}

CTextEncodingDetector::DecodedText CTextEncodingDetector::decode(const QByteArray & textData, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages)
{
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

CTextEncodingDetector::DecodedText CTextEncodingDetector::decode(QIODevice & textDevice, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages)
{
	const auto detectionResult = detect(textDevice, tablesForLanguages);
	if (!detectionResult.empty() && detectionResult.front().score < plausibleMatchThreshold)
	{
		QTextCodec * codec = QTextCodec::codecForName(detectionResult.front().encoding.toUtf8().data());
		assert_r(codec);
		if (codec)
			return DecodedText{codec->toUnicode(textDevice.readAll()), detectionResult.front().encoding, detectionResult.front().language};
	}

	return DecodedText();
}

std::vector<CTextEncodingDetector::EncodingDetectionResult> CTextEncodingDetector::detect(const QString & textFilePath, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages)
{
	return ::detect(textFilePath, tablesForLanguages);
}

std::vector<CTextEncodingDetector::EncodingDetectionResult> CTextEncodingDetector::detect(const QByteArray & textData, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages)
{
	return ::detect(textData, tablesForLanguages);
}

std::vector<CTextEncodingDetector::EncodingDetectionResult> CTextEncodingDetector::detect(QIODevice & textDevice, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages)
{
	return ::detect(textDevice, tablesForLanguages);
}
