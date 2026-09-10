#include "ctextencodingdetector.h"
#include "trigramfrequencytables/ctrigramfrequencytable_french.h"
#include "trigramfrequencytables/ctrigramfrequencytable_german.h"
#include "trigramfrequencytables/ctrigramfrequencytable_polish.h"
#include "trigramfrequencytables/ctrigramfrequencytable_russian.h"
#include "trigramfrequencytables/ctrigramfrequencytable_spanish.h"

#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QTextCodec>
RESTORE_COMPILER_WARNINGS

#include <array>
#include <algorithm>
#include <cmath>
#include <string.h> // memcmp
#include <memory>
#include <ranges>

// cosineDistance(): 0.0 is best, 1.0 means no useful trigram overlap.
static constexpr double plausibleMatchThreshold = 0.95;

[[nodiscard]] inline bool startsWithBytes(const QByteArray& data, const char* bytes, int bytesSize) noexcept
{
	return data.size() >= bytesSize && ::memcmp(data.constData(), bytes, bytesSize) == 0;
}

bool isBinary(const QByteArray& data)
{
	return data.contains('\0');
}

bool isUtf8(const QByteArray& data)
{
	const QString text = QString::fromUtf8(data);
	return text.toUtf8() == data;
}

namespace {

// NUL bytes counted by offset modulo 4. BOM-less UTF-16 and UTF-32 keep their NULs in fixed phases; binary does not.
struct NulPhases
{
	qsizetype count[4] = {};
	qsizetype positions = 0; // Bytes in each phase, the denominator of a phase's share

	[[nodiscard]] bool any() const noexcept { return count[0] + count[1] + count[2] + count[3] > 0; }
	[[nodiscard]] double share(int phase) const noexcept { return positions > 0 ? static_cast<double>(count[phase]) / static_cast<double>(positions) : 0.0; }
};

[[nodiscard]] NulPhases countNulPhases(const QByteArray& data) noexcept
{
	NulPhases phases;
	if (!data.contains('\0')) // memchr first: NUL-free input, the common case, pays one pass at memory speed
		return phases;

	const char* const bytes = data.constData();
	for (qsizetype i = 0; i < data.size(); ++i)
	{
		if (bytes[i] == '\0')
			++phases.count[i & 3];
	}

	phases.positions = data.size() / 4;
	return phases;
}

// The share of a phase's bytes that are NUL where the phase is a wide encoding's zero byte, and the most where it is
// a data byte. Latin text in UTF-16 has every high byte zero; Cyrillic only its spaces and punctuation, about a fifth.
constexpr double wideZeroPhaseMinShare = 0.10;
constexpr double wideDataPhaseMaxShare = 0.01;
// UTF-32's two upper bytes are zero for the whole basic plane
constexpr double utf32ZeroPhaseMinShare = 0.90;

// The BOM-less wide encoding the NUL layout fits, or nullptr for a binary one. UTF-32 is tried first: its layout
// passes the UTF-16 test of the same endianness.
[[nodiscard]] const char* wideEncodingFor(const NulPhases& nuls, qsizetype size) noexcept
{
	if (size % 4 == 0)
	{
		if (nuls.share(2) >= utf32ZeroPhaseMinShare && nuls.share(3) >= utf32ZeroPhaseMinShare && nuls.share(0) <= wideDataPhaseMaxShare)
			return "UTF-32LE";
		if (nuls.share(0) >= utf32ZeroPhaseMinShare && nuls.share(1) >= utf32ZeroPhaseMinShare && nuls.share(3) <= wideDataPhaseMaxShare)
			return "UTF-32BE";
	}

	if (size % 2 == 0)
	{
		const double even = (nuls.share(0) + nuls.share(2)) / 2.0;
		const double odd = (nuls.share(1) + nuls.share(3)) / 2.0;
		if (odd >= wideZeroPhaseMinShare && even <= wideDataPhaseMaxShare)
			return "UTF-16LE";
		if (even >= wideZeroPhaseMinShare && odd <= wideDataPhaseMaxShare)
			return "UTF-16BE";
	}

	return nullptr;
}

// An array of small integers has a wide encoding's NUL layout too, and decodes to Latin-1 letters, control
// characters and replacement characters in equal measure. Text is three quarters letters, digits and whitespace
// (the JSON host is the lowest of the corpus at 85%), and carries next to no control characters beyond whitespace.
constexpr double textWordyMinShare = 0.75;
constexpr double textJunkMaxShare = 0.001;

[[nodiscard]] bool looksLikeText(const QString& text) noexcept
{
	if (text.isEmpty())
		return false;

	qsizetype wordy = 0;
	qsizetype junk = 0;
	for (qsizetype i = 0; i < text.size(); ++i)
	{
		const QChar ch = text[i];
		if (ch.isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate())
		{
			++i; // A pair is one character outside the basic plane, neither wordy nor junk
			continue;
		}

		if (ch.isLetterOrNumber() || ch.isSpace())
			++wordy;
		else if (ch.isSurrogate() || ch == QChar::ReplacementCharacter || (ch.category() == QChar::Other_Control && !ch.isSpace()))
			++junk;
	}

	const auto size = static_cast<double>(text.size());
	return static_cast<double>(wordy) >= textWordyMinShare * size && static_cast<double>(junk) <= textJunkMaxShare * size;
}

}

[[nodiscard]] inline double logProbabilityScore(const CTextParser::OccurrenceTable& model, const CTextParser::OccurrenceTable& sample) noexcept
{
	static constexpr double Lmax = 15.0;
	if (sample.totalTrigramsCount <= 5) // Too little data to draw conclusions
		return 1e20;

	double totalLoss = 0.0;
	quint64 totalCount = 0;

	for (const auto& [trigram, stats] : sample.trigramOccurrenceTable)
	{
		const quint64 count = stats.rawCount;
		const auto it = model.trigramOccurrenceTable.find(trigram);

		const double loss =
			(it != model.trigramOccurrenceTable.end())
			? std::min(static_cast<double>(it->second.loss), Lmax)
			: Lmax;

		totalLoss += static_cast<double>(count) * loss;
		totalCount += count;
	}

	if (totalCount == 0)
		return 1e20; // arbitrary large number to represent no match

	return totalLoss / static_cast<double>(totalCount);
}

// Only the trigrams with a non-ASCII character count, on both sides: the ASCII ones decode the same under every
// codec and would only dilute. A reading that turns the non-ASCII into replacement characters keeps no trigram
// at all and scores 1.0; one that maps it to the wrong letters keeps trigrams the table has never seen.
[[nodiscard]] inline double cosineDistance(const CTrigramFrequencyTable_Base& model, const CTextParser::OccurrenceTable& sample) noexcept
{
	const double unknownPenaltyWeight = 0.0; // Adjust this weight to control the penalty for unknown trigrams
	const auto& modelTable = model.trigramOccurrenceTable().trigramOccurrenceTable;
	if (modelTable.empty() || sample.trigramOccurrenceTable.empty())
		return 1.0 + unknownPenaltyWeight;

	double dot = 0.0;
	double sampleNormSq = 0.0;
	double unknownCount = 0.0;
	double sampleTotalCount = 0.0;

	for (const auto& [trigram, sampleStats] : sample.trigramOccurrenceTable)
	{
		if (!trigram.hasNonAsciiCharacter())
			continue;

		const double sampleCount = static_cast<double>(sampleStats.rawCount);
		sampleNormSq += sampleCount * sampleCount;
		sampleTotalCount += sampleCount;

		const auto modelIt = modelTable.find(trigram);
		if (modelIt != modelTable.end())
		{
			const double modelCount = static_cast<double>(modelIt->second.rawCount);
			dot += sampleCount * modelCount;
		}
		else
		{
			unknownCount += sampleCount;
		}
	}

	const double modelNormSq = model.countsNormSquared();
	if (modelNormSq <= 0.0 || sampleNormSq <= 0.0 || sampleTotalCount <= 0.0)
		return 1.0 + unknownPenaltyWeight;

	const double similarity = dot / (std::sqrt(modelNormSq) * std::sqrt(sampleNormSq));
	const double clampedSimilarity = std::clamp(similarity, 0.0, 1.0);

	const double cosineDistance = 1.0 - clampedSimilarity;
	const double unknownFraction = unknownCount / sampleTotalCount;

	return cosineDistance + unknownPenaltyWeight * unknownFraction;
}

template <typename Container, typename Value>
inline bool contains(const Container& container, const Value& value)
{
	return std::ranges::find(container, value) != container.end();
}

// Read-only once built, so one instance serves every call from every thread.
// No English table: its trigrams would all be ASCII, which scoring ignores, and pure ASCII never reaches detect().
[[nodiscard]] static const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& defaultLanguageTables()
{
	static const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>> tables = [] {
		std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>> built;
		built.push_back(std::make_unique<CTrigramFrequencyTable_French>());
		built.push_back(std::make_unique<CTrigramFrequencyTable_German>());
		built.push_back(std::make_unique<CTrigramFrequencyTable_Polish>());
		built.push_back(std::make_unique<CTrigramFrequencyTable_Russian>());
		built.push_back(std::make_unique<CTrigramFrequencyTable_Spanish>());
		return built;
	}();

	return tables;
}

CTextEncodingDetector::DecodedText CTextEncodingDetector::decode(const QByteArray & textData, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages)
{
	if (auto decodedText = decodeUtfBom(textData); !decodedText.encoding.isEmpty())
		return decodedText;

	// The only texts with a NUL byte are BOM-less UTF-16 and UTF-32; anything else that carries one is binary
	if (const NulPhases nuls = countNulPhases(textData); nuls.any())
	{
		const char* const encoding = wideEncodingFor(nuls, textData.size());
		if (!encoding)
			return {};

		QTextCodec* const codec = QTextCodec::codecForName(encoding);
		assert_r(codec);
		if (!codec)
			return {};

		QString text = codec->toUnicode(textData);
		if (!looksLikeText(text))
			return {};

		return DecodedText{ std::move(text), encoding, {}, 0.0 };
	}

	if (isUtf8(textData))
		return DecodedText{QString::fromUtf8(textData), "UTF-8", {}, 0.0};

	const auto detectionResult = detect(anchoredSample(textData), tablesForLanguages);
	if (!detectionResult.empty() && detectionResult.front().score < plausibleMatchThreshold)
	{
		const auto& best = detectionResult.front();
		QTextCodec * codec = QTextCodec::codecForName(best.encoding.toUtf8().data());
		assert_r(codec);
		if (codec)
			return DecodedText{codec->toUnicode(textData), best.encoding, best.language, best.score};
	}

	return DecodedText();
}

QByteArray CTextEncodingDetector::anchoredSample(const QByteArray& data, qsizetype budgetBytes, qsizetype chunkBytes)
{
	if (data.size() <= budgetBytes)
		return data;

	const auto* const bytes = reinterpret_cast<const unsigned char*>(data.constData());
	qsizetype anchorCount = 0;
	for (qsizetype i = 0; i < data.size(); ++i)
		anchorCount += bytes[i] >= 0x80;

	// Never more chunks than anchors: past that the same bytes come back repeatedly
	const qsizetype chunks = std::min(budgetBytes / chunkBytes, anchorCount);

	QByteArray sample;
	sample.reserve(chunks * chunkBytes);

	// The anchors of rank anchorCount * c / chunks, taken in a second pass: a list of every non-ASCII offset would
	// outweigh a large Cyrillic file
	qsizetype rank = 0;
	for (qsizetype i = 0, c = 0; i < data.size() && c < chunks; ++i)
	{
		if (bytes[i] < 0x80 || rank++ != anchorCount * c / chunks)
			continue;

		const qsizetype start = std::clamp<qsizetype>(i - chunkBytes / 2, 0, data.size() - chunkBytes);
		sample.append(data.constData() + start, chunkBytes);
		++c;
	}

	return sample;
}

CTextEncodingDetector::DecodedText CTextEncodingDetector::decodeUtfBom(const QByteArray& textData)
{
	struct BomEncoding
	{
		const char* bytes;
		int bytesSize;
		const char* encoding;
	};

	static constexpr std::array bomEncodings {
		BomEncoding{ "\xFF\xFE\x00\x00", 4, "UTF-32LE" },
		BomEncoding{ "\x00\x00\xFE\xFF", 4, "UTF-32BE" },
		BomEncoding{ "\xEF\xBB\xBF", 3, "UTF-8" },
		BomEncoding{ "\xFF\xFE", 2, "UTF-16LE" },
		BomEncoding{ "\xFE\xFF", 2, "UTF-16BE" },
	};

	for (const auto& bomEncoding : bomEncodings)
	{
		if (!startsWithBytes(textData, bomEncoding.bytes, bomEncoding.bytesSize))
			continue;

		QTextCodec* codec = QTextCodec::codecForName(bomEncoding.encoding);
		assert_r(codec);
		if (!codec)
			return {};

		return DecodedText{
			codec->toUnicode(textData.constData() + bomEncoding.bytesSize, static_cast<int>(textData.size() - bomEncoding.bytesSize)),
			bomEncoding.encoding,
			{},
			0.0
		};
	}

	return {};
}

namespace {

// Windows-1252 ahead of ISO-8859-1 and ISO-8859-15, Windows-1250 ahead of ISO-8859-2: the text they read alike
// is more often the Windows one. No Mac Cyrillic or Mac Central European: Qt has no codec for either.
constexpr std::array eightBitCodecs {
	"Windows-1251", "KOI8-R", "KOI8-U", "CP866", "ISO-8859-5",
	"Windows-1252", "ISO-8859-1", "ISO-8859-15", "macintosh",
	"Windows-1250", "ISO-8859-2",
};

}

std::span<const char* const> CTextEncodingDetector::codecShortlist() noexcept
{
	return eightBitCodecs;
}

std::vector<CTextEncodingDetector::EncodingDetectionResult> CTextEncodingDetector::detect(const QByteArray & textData, const std::vector<std::unique_ptr<CTrigramFrequencyTable_Base>>& tablesForLanguages)
{
	std::vector<QTextCodec*> codecs;
	codecs.reserve(eightBitCodecs.size() + 1);

	for (const char* encodingName : eightBitCodecs)
	{
		QTextCodec* codec = QTextCodec::codecForName(encodingName);
		if (codec && !contains(codecs, codec))
			codecs.push_back(codec);
	}

	if (auto* localeCodec = QTextCodec::codecForLocale(); localeCodec && !contains(codecs, localeCodec))
		codecs.push_back(localeCodec);

	const auto& languageStatisticsTables = tablesForLanguages.empty() ? defaultLanguageTables() : tablesForLanguages;

	std::vector<uint64_t> hashes;
	hashes.reserve(codecs.size());

	std::vector<CTextEncodingDetector::EncodingDetectionResult> match;
	match.reserve(codecs.size() * languageStatisticsTables.size());

	for (const auto& codec : codecs)
	{
		const std::unique_ptr<QTextDecoder> decoder{ codec->makeDecoder() };
		const QString decodedText = decoder->toUnicode(textData);

		// Skip duplicate codecs that produce the same decoded text
		const auto hash = ::wheathash64(decodedText.constData(), decodedText.size() * sizeof(QChar));
		if (contains(hashes, hash))
			continue;

		hashes.push_back(hash);

		CTextParser parser;
		if (!parser.parse(decodedText))
			continue;

		for (const auto& table : languageStatisticsTables)
		{
			const double distanceScore = cosineDistance(*table, parser.parsingResult());
			match.emplace_back(CTextEncodingDetector::EncodingDetectionResult{ codec->name(), table->language(), distanceScore });
		}
	}

	std::ranges::sort(match, std::less{}, &CTextEncodingDetector::EncodingDetectionResult::score);
	return match;
}
