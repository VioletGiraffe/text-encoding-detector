#pragma once

#include "trigramfrequencytables/ctrigramfrequencytable_base.h"

#include <memory>
#include <vector>

class CTrigramFrequencyTable_Base;
class QByteArray;

// A NUL byte anywhere: no 8-bit or UTF-8 text carries one
[[nodiscard]] bool isBinary(const QByteArray& data);
[[nodiscard]] bool isUtf8(const QByteArray& data);

class CTextEncodingDetector
{
public:
	struct DecodedText
	{
		QString text;
		QString encoding;
		QString language;
		// Distance of the chosen reading from its language model, lower is better. 0.0 where the bytes prove
		// the encoding (a BOM, valid UTF-8); 1.0 where nothing was decoded.
		double score = 1.0;
	};

	struct EncodingDetectionResult {
		QString encoding;
		QString language;
		double score; // Lower is better, 0.0 means perfect match
	};

	// In order: a BOM; BOM-less UTF-16/32 by its NUL layout, with any other NUL-carrying input declined as binary;
	// valid UTF-8; then the 8-bit codecs scored against the language tables. Empty where nothing is plausible.
	[[nodiscard]] static DecodedText
	decode(const QByteArray& textData, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages = std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>());

	[[nodiscard]] static DecodedText decodeUtfBom(const QByteArray& textData);

	// The results are sorted by score from best to worst
	[[nodiscard]] static std::vector<EncodingDetectionResult>
	detect(const QByteArray& textData, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages = std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>());
};
