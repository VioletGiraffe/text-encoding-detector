#include "ctextencodingdetector.h"
#include "trigramfrequencytables/ctrigramfrequencytable_english.h"
#include "trigramfrequencytables/ctrigramfrequencytable_russian.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QTextCodec>
#include <QIODevice>
#include <QDebug>
#include <QFile>
#include <QTime>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <limits>
#include <memory>
#include <set>

#include <math.h>

const float plausibleMatchThreshold = 0.1f;

static const CTextEncodingDetector::MatchFunction defaultMatchFunction =
		CTextEncodingDetector::MatchFunction([](const CTextParser::OccurrenceTable& arg1, const CTextParser::OccurrenceTable& arg2) -> float {

			if (arg1.trigramOccurrenceTable.empty() || arg2.trigramOccurrenceTable.empty())
				return 0.0f;
			else if (arg2.trigramOccurrenceTable.size() < arg1.trigramOccurrenceTable.size())
				return defaultMatchFunction(arg2, arg1); // Performance optimization - the outer loop must iterate the smaller of the two containers for better performance

			float deviation = 0.0f;
			for (auto& n_gram1: arg1.trigramOccurrenceTable)
			{
				auto n_gram2 = arg2.trigramOccurrenceTable.find(n_gram1.first);
				deviation += n_gram2 != arg2.trigramOccurrenceTable.end() ?
					fabs(n_gram1.second / (float) arg1.totalTrigramsCount - n_gram2->second / (float) arg2.totalTrigramsCount) :
					n_gram1.second / (float) arg1.totalTrigramsCount;
			}

			return deviation > 1e-5 ? 1.0f / deviation - 1.0f : std::numeric_limits<float>::max();
});

template <typename T>
std::vector<CTextEncodingDetector::EncodingDetectionResult> detect(T& dataOrInputDevice, std::vector<std::shared_ptr<CTrigramFrequencyTable_Base>> tablesForLanguages, CTextEncodingDetector::MatchFunction matchFunction)
{
	QTime start;
	start.start();
	auto availableCodecs = QTextCodec::availableCodecs();
	std::vector<CTextEncodingDetector::EncodingDetectionResult> match;

	if (tablesForLanguages.empty())
	{
		tablesForLanguages.emplace_back(std::make_shared<CTrigramFrequencyTable_English>());
		tablesForLanguages.emplace_back(std::make_shared<CTrigramFrequencyTable_Russian>());
	}

	if (!matchFunction)
		matchFunction = defaultMatchFunction;

	std::set<QTextCodec*> differentCodecs;
	for (const auto& codecName: availableCodecs)
		if (!QString(codecName).toLower().contains("utf-8"))
			differentCodecs.insert(QTextCodec::codecForName(codecName.data()));

	for (auto& codec: differentCodecs)
	{
		CTextParser parser;
		if (!parser.parse(dataOrInputDevice, QString(codec->name())))
			continue;

		for (auto& table: tablesForLanguages)
			match.emplace_back(CTextEncodingDetector::EncodingDetectionResult(codec->name(), table->language(), matchFunction(table->trigramOccurrenceTable(), parser.parsingResult())));
	}

	std::sort(match.begin(), match.end(), [](const CTextEncodingDetector::EncodingDetectionResult& l, const CTextEncodingDetector::EncodingDetectionResult& r){return l.match > r.match;});
	qDebug() << __FUNCTION__ << "Time taken:" << start.elapsed() << "ms";
	return match;
}


CTextEncodingDetector::DetectionResult CTextEncodingDetector::decode(const QString & textFilePath, std::vector<std::shared_ptr<CTrigramFrequencyTable_Base> > tablesForLanguages, CTextEncodingDetector::MatchFunction customMatchFunction)
{
	auto detectionResult = detect(textFilePath, tablesForLanguages, customMatchFunction);
	qDebug() << "Encoding detection result for" << textFilePath;
	for (auto& match: detectionResult)
		qDebug() << QString("%1, %2: %3").arg(match.language).arg(match.encoding).arg(match.match);

	if (!detectionResult.empty() && detectionResult.front().match > plausibleMatchThreshold)
	{
		QTextCodec * codec = QTextCodec::codecForName(detectionResult.front().encoding.toUtf8().data());
		assert_r(codec);
		if (codec)
		{
			QFile file(textFilePath);
			file.open(QIODevice::ReadOnly);
			return DetectionResult{codec->toUnicode(file.readAll()), detectionResult.front().encoding, detectionResult.front().language};
		}
	}

	return DetectionResult();
}

CTextEncodingDetector::DetectionResult CTextEncodingDetector::decode(const QByteArray & textData, std::vector<std::shared_ptr<CTrigramFrequencyTable_Base> > tablesForLanguages, CTextEncodingDetector::MatchFunction customMatchFunction)
{
	auto detectionResult = detect(textData, tablesForLanguages, customMatchFunction);
	qDebug() << "Encoding detection result:";
	for (auto& match: detectionResult)
		qDebug() << QString("%1, %2: %3").arg(match.language).arg(match.encoding).arg(match.match);

	if (!detectionResult.empty() && detectionResult.front().match > plausibleMatchThreshold)
	{
		QTextCodec * codec = QTextCodec::codecForName(detectionResult.front().encoding.toUtf8().data());
		assert_r(codec);
		if (codec)
			return DetectionResult{codec->toUnicode(textData), detectionResult.front().encoding, detectionResult.front().language};
	}

	return DetectionResult();
}

CTextEncodingDetector::DetectionResult CTextEncodingDetector::decode(QIODevice & textDevice, std::vector<std::shared_ptr<CTrigramFrequencyTable_Base> > tablesForLanguages, CTextEncodingDetector::MatchFunction customMatchFunction)
{
	auto detectionResult = detect(textDevice, tablesForLanguages, customMatchFunction);
	qDebug() << "Encoding detection result:";
	for (auto& match: detectionResult)
		qDebug() << QString("%1, %2: %3").arg(match.language).arg(match.encoding).arg(match.match);

	if (!detectionResult.empty() && detectionResult.front().match > plausibleMatchThreshold)
	{
		QTextCodec * codec = QTextCodec::codecForName(detectionResult.front().encoding.toUtf8().data());
		assert_r(codec);
		if (codec)
			return DetectionResult{codec->toUnicode(textDevice.readAll()), detectionResult.front().encoding, detectionResult.front().language};
	}

	return DetectionResult();
}

std::vector<CTextEncodingDetector::EncodingDetectionResult> CTextEncodingDetector::detect(const QString & textFilePath, std::vector<std::shared_ptr<CTrigramFrequencyTable_Base> > tablesForLanguages, MatchFunction customMatchFunction)
{
	return ::detect(textFilePath, tablesForLanguages, customMatchFunction);
}

std::vector<CTextEncodingDetector::EncodingDetectionResult> CTextEncodingDetector::detect(const QByteArray & textData, std::vector<std::shared_ptr<CTrigramFrequencyTable_Base> > tablesForLanguages, MatchFunction customMatchFunction)
{
	return ::detect(textData, tablesForLanguages, customMatchFunction);
}

std::vector<CTextEncodingDetector::EncodingDetectionResult> CTextEncodingDetector::detect(QIODevice & textDevice, std::vector<std::shared_ptr<CTrigramFrequencyTable_Base> > tablesForLanguages, MatchFunction customMatchFunction)
{
	return ::detect(textDevice, tablesForLanguages, customMatchFunction);
}
