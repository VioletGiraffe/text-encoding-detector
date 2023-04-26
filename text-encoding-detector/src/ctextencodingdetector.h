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
		float match; // 0.0 to 1.0
	};

	[[nodiscard]] static DecodedText
	decode(const QString& textFilePath, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages = std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>());
	[[nodiscard]] static DecodedText
	decode(const QByteArray& textData, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages = std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>());
	[[nodiscard]] static DecodedText
	decode(QIODevice& textDevice, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages = std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>());


	// The results are sorted by match from high to low
	[[nodiscard]] static std::vector<EncodingDetectionResult>
	detect(const QString& textFilePath, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages = std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>());

	[[nodiscard]] static std::vector<EncodingDetectionResult>
	detect(const QByteArray& textData, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages = std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>());

	[[nodiscard]] static std::vector<EncodingDetectionResult>
	detect(QIODevice& textDevice, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages = std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>());
};
