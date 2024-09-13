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
#include <unordered_set>

#include <math.h>

static constexpr float plausibleMatchThreshold = 0.1f;

inline float defaultMatchFunction(const CTextParser::OccurrenceTable& arg1, const CTextParser::OccurrenceTable& arg2)
{
	if (arg1.trigramOccurrenceTable.empty() || arg2.trigramOccurrenceTable.empty())
		return 0.0f;

	const auto& largerTable = arg1.trigramOccurrenceTable.size() > arg2.trigramOccurrenceTable.size() ? arg1: arg2;
	const auto& smallerTable = arg1.trigramOccurrenceTable.size() <= arg2.trigramOccurrenceTable.size() ? arg1: arg2;

	// Performance optimization: it's faster to make a smaller number of lookups into a larger hash map than vice versa.

	float deviation = 0.0f;
	for (const auto& n_gram1: smallerTable.trigramOccurrenceTable.asKeyValueRange())
	{
		const float n_gram1Ratio = (float)n_gram1.second / (float)smallerTable.totalTrigramsCount;

		const auto n_gram2 = largerTable.trigramOccurrenceTable.find(n_gram1.first);
		deviation += n_gram2 != largerTable.trigramOccurrenceTable.end() ?
			fabs(n_gram1Ratio - (float)n_gram2.value() / (float)largerTable.totalTrigramsCount) :
			n_gram1Ratio;
	}

	return deviation > 1e-5f ? (1.0f / deviation - 1.0f) : float_max;
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
		if (!QString(codecName).contains(QSL("utf-8"), Qt::CaseInsensitive))
			differentCodecs.insert(QTextCodec::codecForName(codecName.data()));
	}

	std::vector<CTextEncodingDetector::EncodingDetectionResult> match;
	for (const auto& codec: differentCodecs)
	{
		CTextParser parser;
		if (!parser.parse(dataOrInputDevice, QString(codec->name())))
			continue;

		const auto& languageStatisticsTables = tablesForLanguages.empty() ? defaultTables : tablesForLanguages;
		for (const auto& table: languageStatisticsTables)
			match.emplace_back(CTextEncodingDetector::EncodingDetectionResult{ codec->name(), table->language(), defaultMatchFunction(table->trigramOccurrenceTable(), parser.parsingResult()) });
	}

	std::sort(match.begin(), match.end(), [](const CTextEncodingDetector::EncodingDetectionResult& l, const CTextEncodingDetector::EncodingDetectionResult& r){return l.match > r.match;});
	return match;
}


CTextEncodingDetector::DecodedText CTextEncodingDetector::decode(const QString & textFilePath, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages)
{
	const auto detectionResult = detect(textFilePath, tablesForLanguages);
	if (!detectionResult.empty() && detectionResult.front().match > plausibleMatchThreshold)
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
	if (!detectionResult.empty() && detectionResult.front().match > plausibleMatchThreshold)
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
	if (!detectionResult.empty() && detectionResult.front().match > plausibleMatchThreshold)
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
