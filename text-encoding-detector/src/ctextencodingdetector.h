#pragma once

#include "trigramfrequencytables/ctrigramfrequencytable_base.h"

#include <memory>
#include <optional>
#include <span>
#include <vector>

class CTrigramFrequencyTable_Base;
class QByteArray;

// A NUL byte anywhere: no 8-bit or UTF-8 text carries one
[[nodiscard]] bool isBinary(const QByteArray& data);
// Empty where the bytes are not UTF-8. A multi-byte sequence the end of the input cuts short is not an error:
// the characters before it decode, and the partial one is dropped.
[[nodiscard]] std::optional<QString> decodeUtf8(const QByteArray& data);

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
	// valid UTF-8; then the 8-bit codecs scored against the language tables, on anchoredSample() of the input.
	// Empty where nothing is plausible.
	[[nodiscard]] static DecodedText
	decode(const QByteArray& textData, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages = std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>());

	// decode(), read as the system locale's 8-bit codec where it finds nothing plausible: every input yields some text.
	[[nodiscard]] static DecodedText decodeWithLocaleFallback(const QByteArray& textData);

	[[nodiscard]] static DecodedText decodeUtfBom(const QByteArray& textData);

	// The UTF-16 or UTF-32 encoding the layout of the NUL bytes fits, by Qt codec name. Neither a BOM nor the decoded text is checked.
	// nullptr for NUL-free input and for a layout no wide encoding fits.
	[[nodiscard]] static const char* wideEncodingFromNulLayout(const QByteArray& data) noexcept;

	// Scores the whole of 'textData' under every codec of the shortlist and the locale's, against every table.
	// The results are sorted by score from best to worst. Wide encodings and UTF-8 are decode()'s business, not this.
	[[nodiscard]] static std::vector<EncodingDetectionResult>
	detect(const QByteArray& textData, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages = std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>());

	// The 8-bit codecs detect() tries, by the Qt names it resolves them with. Order breaks a tie between codecs
	// that read the same bytes as the same text.
	[[nodiscard]] static std::span<const char* const> codecShortlist() noexcept;

	static constexpr qsizetype detectionSampleBudget = 256 * 1024;
	// Small chunks rather than few large ones: the ASCII around a non-ASCII byte decodes the same under every
	// codec and only takes budget from the bytes that decide
	static constexpr qsizetype detectionSampleChunk = 128;

	// The bytes decode() has detect() score for an input over the budget: chunks centred on evenly spaced
	// non-ASCII bytes, the only ones the 8-bit codecs disagree on. Constant cost, every part of the file represented.
	// The input itself when it fits the budget; empty for all-ASCII input, which never reaches detection.
	[[nodiscard]] static QByteArray anchoredSample(const QByteArray& data, qsizetype budgetBytes = detectionSampleBudget, qsizetype chunkBytes = detectionSampleChunk);
};
