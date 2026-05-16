#pragma once

#include "trigramfrequencytables/ctrigramfrequencytable_base.h"

#include <memory>
#include <vector>

class CTrigramFrequencyTable_Base;
class QIODevice;
class QByteArray;

class CTextEncodingDetector
{
public:
	struct DecodedText
	{
		QString text;
		QString encoding;
		QString language;
	};

	struct EncodingDetectionResult {
		QString encoding;
		QString language;
		double score; // Lower is better, 0.0 means perfect match
	};

	[[nodiscard]] static DecodedText
	decode(const QByteArray& textData, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages = std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>());

	// The results are sorted by score from best to worst
	[[nodiscard]] static std::vector<EncodingDetectionResult>
	detect(const QByteArray& textData, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages = std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>());
};
