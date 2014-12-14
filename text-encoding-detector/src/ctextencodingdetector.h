#ifndef CTEXTENCODINGDETECTOR_H
#define CTEXTENCODINGDETECTOR_H

#include "ctextparser.h"

#include <QString>

#include <utility>
#include <functional>
#include <vector>
#include <memory>

class CTrigramFrequencyTable_Base;
class QIODevice;
class QByteArray;

class CTextEncodingDetector
{
public:
	typedef std::function<float (const CTextParser::OccurrenceTable& arg1, const CTextParser::OccurrenceTable& arg2)> MatchFunction;

	struct EncodingDetectionResult {
		EncodingDetectionResult(const QString& encoding_, const QString& language_, float match_) : encoding(encoding_), language(language_), match(match_) {}
		QString encoding;
		QString language;
		float match; // 0.0 to 1.0
	};

	// The results are sorted by match from high to low
	static std::vector<EncodingDetectionResult>
	detect(const QString& textFilePath, std::vector<std::shared_ptr<CTrigramFrequencyTable_Base> > tablesForLanguages = std::vector<std::shared_ptr<CTrigramFrequencyTable_Base> >(), MatchFunction customMatchFunction = MatchFunction());

	static std::vector<EncodingDetectionResult>
	detect(const QByteArray& textData, std::vector<std::shared_ptr<CTrigramFrequencyTable_Base> > tablesForLanguages = std::vector<std::shared_ptr<CTrigramFrequencyTable_Base> >(), MatchFunction customMatchFunction = MatchFunction());

	static std::vector<EncodingDetectionResult>
	detect(QIODevice& textDevice, std::vector<std::shared_ptr<CTrigramFrequencyTable_Base> > tablesForLanguages = std::vector<std::shared_ptr<CTrigramFrequencyTable_Base> >(), MatchFunction customMatchFunction = MatchFunction());
};

#endif // CTEXTENCODINGDETECTOR_H
