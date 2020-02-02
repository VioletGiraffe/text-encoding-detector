#include "ctextencodingdetector.h"
#include "trigramfrequencytables/ctrigramfrequencytable_english.h"
#include "trigramfrequencytables/ctrigramfrequencytable_russian.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QTextCodec>
#include <QIODevice>
#include <QDebug>
#include <QFile>
#include <QElapsedTimer>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <limits>
#include <memory>
#include <set>

#include <math.h>

const float plausibleMatchThreshold = 0.1f;

inline float defaultMatchFunction(const CTextParser::OccurrenceTable& arg1, const CTextParser::OccurrenceTable& arg2)
{
	if (arg1.trigramOccurrenceTable.empty() || arg2.trigramOccurrenceTable.empty())
		return 0.0f;
	else if (arg2.trigramOccurrenceTable.size() < arg1.trigramOccurrenceTable.size())
		return defaultMatchFunction(arg2, arg1); // Performance optimization: the outer loop must iterate the smaller of the two containers for better performance

	float deviation = 0.0f;
	for (auto& n_gram1: arg1.trigramOccurrenceTable)
	{
		auto n_gram2 = arg2.trigramOccurrenceTable.find(n_gram1.first);
		deviation += n_gram2 != arg2.trigramOccurrenceTable.end() ?
			fabs(n_gram1.second / (float) arg1.totalTrigramsCount - n_gram2->second / (float) arg2.totalTrigramsCount) :
			n_gram1.second / (float) arg1.totalTrigramsCount;
	}

	return deviation > 1e-5f ? 1.0f / deviation - 1.0f : std::numeric_limits<float>::max();
}

template <typename T>
std::vector<CTextEncodingDetector::EncodingDetectionResult> detect(T& dataOrInputDevice, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages)
{
    QElapsedTimer start;
	start.start();
	auto availableCodecs = QTextCodec::availableCodecs();
	std::vector<CTextEncodingDetector::EncodingDetectionResult> match;

	typename std::decay<decltype(tablesForLanguages)>::type defaultTables;
	if (tablesForLanguages.empty())
	{
		defaultTables.emplace_back(std::make_unique<CTrigramFrequencyTable_English>());
		defaultTables.emplace_back(std::make_unique<CTrigramFrequencyTable_Russian>());
	}

	std::set<QTextCodec*> differentCodecs;
	for (const auto& codecName: availableCodecs)
		if (!QString(codecName).contains("utf-8", Qt::CaseInsensitive))
			differentCodecs.insert(QTextCodec::codecForName(codecName.data()));

	for (auto& codec: differentCodecs)
	{
		CTextParser parser;
		if (!parser.parse(dataOrInputDevice, QString(codec->name())))
			continue;

		const auto& languageStatisticsTables = tablesForLanguages.empty() ? defaultTables : tablesForLanguages;
		for (auto& table: languageStatisticsTables)
			match.emplace_back(CTextEncodingDetector::EncodingDetectionResult{ codec->name(), table->language(), defaultMatchFunction(table->trigramOccurrenceTable(), parser.parsingResult()) });
	}

	std::sort(match.begin(), match.end(), [](const CTextEncodingDetector::EncodingDetectionResult& l, const CTextEncodingDetector::EncodingDetectionResult& r){return l.match > r.match;});
#ifdef _DEBUG
	qInfo() << __FUNCTION__ << "Time taken:" << start.elapsed() << "ms";
#endif
	return match;
}


CTextEncodingDetector::DecodedText CTextEncodingDetector::decode(const QString & textFilePath, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages)
{
	auto detectionResult = detect(textFilePath, tablesForLanguages);
#ifdef _DEBUG
	qInfo() << "Encoding detection result for" << textFilePath;
	for (auto& match: detectionResult)
		qInfo() << QString("%1, %2: %3").arg(match.language, match.encoding, QString::number((double)match.match));
#endif

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
	auto detectionResult = detect(textData, tablesForLanguages);
#ifdef _DEBUG
	qInfo() << "Encoding detection result:";
	for (auto& match: detectionResult)
		qInfo() << QString("%1, %2: %3").arg(match.language, match.encoding, QString::number((double)match.match));
#endif

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
	auto detectionResult = detect(textDevice, tablesForLanguages);
#ifdef _DEBUG
	qInfo() << "Encoding detection result:";
	for (auto& match: detectionResult)
		qInfo() << QString("%1, %2: %3").arg(match.language, match.encoding, QString::number((double)match.match));
#endif

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
