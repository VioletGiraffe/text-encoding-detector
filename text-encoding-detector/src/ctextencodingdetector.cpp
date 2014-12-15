#include "ctextencodingdetector.h"
#include "trigramfrequencytables/ctrigramfrequencytable_english.h"
#include "trigramfrequencytables/ctrigramfrequencytable_russian.h"

#include <QTextCodec>
#include <QIODevice>

#include <algorithm>
#include <set>

static const auto defaultMatchFunction =
		CTextEncodingDetector::MatchFunction([](const CTextParser::OccurrenceTable& arg1, const CTextParser::OccurrenceTable& arg2) -> float {
			if (arg2.trigramOccurrenceTable.size() < arg1.trigramOccurrenceTable.size())
				return defaultMatchFunction(arg2, arg1); // Performance optimization - outer loop must be over the smaller of the 2 containers for better performance

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
std::vector<CTextEncodingDetector::EncodingDetectionResult> detect(T& parameter, std::vector<std::shared_ptr<CTrigramFrequencyTable_Base>> tablesForLanguages, CTextEncodingDetector::MatchFunction matchFunction)
{
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
	{
		if (!QString(codecName).toLower().contains("utf-8"))
		{
			differentCodecs.insert(QTextCodec::codecForName(codecName.data()));
		}
	}

	for (auto& codec: differentCodecs)
	{
		CTextParser parser;
		if (!parser.parse(parameter, QString(codec->name())))
			continue;

		for (auto& table: tablesForLanguages)
			match.emplace_back(CTextEncodingDetector::EncodingDetectionResult(codec->name(), table->language(), matchFunction(table->trigramOccurrenceTable(), parser.parsingResult())));
	}

	std::sort(match.begin(), match.end(), [](const CTextEncodingDetector::EncodingDetectionResult& l, const CTextEncodingDetector::EncodingDetectionResult& r){return l.match > r.match;});
	return match;
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
