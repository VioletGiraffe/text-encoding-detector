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
#include <memory>
#include <set>

static const CTextEncodingDetector::MatchFunction defaultMatchFunction =
		CTextEncodingDetector::MatchFunction([](const CTextParser::OccurrenceTable& arg1, const CTextParser::OccurrenceTable& arg2) -> float {
			if (arg2.trigramOccurrenceTable.size() < arg1.trigramOccurrenceTable.size())
				return defaultMatchFunction(arg2, arg1); // Performance optimization - the outer loop must iterate the smaller of the two containers for better performance

			float match = 0.0f;
			quint64 matchingNgramsCount = 0;
			for (auto& n_gram1: arg1.trigramOccurrenceTable)
			{
				auto n_gram2 = arg2.trigramOccurrenceTable.find(n_gram1.first);
				if (n_gram2 != arg2.trigramOccurrenceTable.end())
				{
					const float intersection = n_gram1.second / ((float)arg1.totalTrigrammsCount * n_gram2->second / (float)arg2.totalTrigrammsCount);
					match += intersection <= 1.0f ? intersection : 1.0f/intersection;
					++matchingNgramsCount;
				}
			}
			return matchingNgramsCount > 0 ? match / matchingNgramsCount : 0.0f;
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
		if (!parser.parse(dataOrInputDevice, QString(codec->name()), 3000))
			continue;

		for (auto& table: tablesForLanguages)
			match.emplace_back(CTextEncodingDetector::EncodingDetectionResult(codec->name(), table->language(), matchFunction(table->trigramOccurrenceTable(), parser.parsingResult())));
	}

	std::sort(match.begin(), match.end(), [](const CTextEncodingDetector::EncodingDetectionResult& l, const CTextEncodingDetector::EncodingDetectionResult& r){return l.match > r.match;});
	qDebug() << __FUNCTION__ << "Time taken:" << start.elapsed() << "ms";
	return match;
}


std::pair<QString, QString> CTextEncodingDetector::decode(const QString & textFilePath, std::vector<std::shared_ptr<CTrigramFrequencyTable_Base> > tablesForLanguages, CTextEncodingDetector::MatchFunction customMatchFunction)
{
	auto detectionResult = detect(textFilePath, tablesForLanguages, customMatchFunction);
	qDebug() << "Encoding detection result for" << textFilePath;
	for (auto& match: detectionResult)
		if (match.match > 0.05f)
			qDebug() << QString("%1, %2: %3").arg(match.language).arg(match.encoding).arg(match.match);

	if (!detectionResult.empty())
	{
		QTextCodec * codec = QTextCodec::codecForName(detectionResult.front().encoding.toUtf8().data());
		assert_r(codec);
		if (codec)
		{
			QFile file(textFilePath);
			file.open(QIODevice::ReadOnly);
			return std::make_pair(codec->toUnicode(file.readAll()), detectionResult.front().encoding);
		}
	}

	return std::pair<QString, QString>();
}

std::pair<QString, QString> CTextEncodingDetector::decode(const QByteArray & textData, std::vector<std::shared_ptr<CTrigramFrequencyTable_Base> > tablesForLanguages, CTextEncodingDetector::MatchFunction customMatchFunction)
{
	auto detectionResult = detect(textData, tablesForLanguages, customMatchFunction);
	qDebug() << "Encoding detection result:";
	for (auto& match: detectionResult)
		if (match.match > 0.05f)
			qDebug() << QString("%1, %2: %3").arg(match.language).arg(match.encoding).arg(match.match);

	if (!detectionResult.empty())
	{
		QTextCodec * codec = QTextCodec::codecForName(detectionResult.front().encoding.toUtf8().data());
		assert_r(codec);
		if (codec)
			return std::make_pair(codec->toUnicode(textData), detectionResult.front().encoding);
	}

	return std::pair<QString, QString>();
}

std::pair<QString, QString> CTextEncodingDetector::decode(QIODevice & textDevice, std::vector<std::shared_ptr<CTrigramFrequencyTable_Base> > tablesForLanguages, CTextEncodingDetector::MatchFunction customMatchFunction)
{
	auto detectionResult = detect(textDevice, tablesForLanguages, customMatchFunction);
	qDebug() << "Encoding detection result:";
	for (auto& match: detectionResult)
		if (match.match > 0.05f)
			qDebug() << QString("%1, %2: %3").arg(match.language).arg(match.encoding).arg(match.match);

	if (!detectionResult.empty())
	{
		QTextCodec * codec = QTextCodec::codecForName(detectionResult.front().encoding.toUtf8().data());
		assert_r(codec);
		if (codec)
			return std::make_pair(codec->toUnicode(textDevice.readAll()), detectionResult.front().encoding);
	}

	return std::pair<QString, QString>();
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
