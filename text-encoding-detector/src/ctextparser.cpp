#include "ctextparser.h"

#include <array>

namespace {

// Latin with its supplements and extensions, IPA, combining marks, Greek and Cyrillic all sit below this. Every
// script the frequency tables cover is tabulated, which keeps the branch in loweredLetter() predictable on prose.
constexpr char16_t tabulatedCodePoints = 0x500;

using LoweredLetters = std::array<char16_t, tabulatedCodePoints>;

// Built from QChar rather than from Unicode data written out here, so the table cannot disagree with the fallback.
// Function-local: building it calls into QtCore, which a namespace-scope initializer would do before main().
[[nodiscard]] const LoweredLetters& loweredLetterTable()
{
	static const LoweredLetters table = [] {
		LoweredLetters built{};
		for (char16_t code = 0; code < tabulatedCodePoints; ++code)
		{
			const QChar c{ code };
			built[code] = c.isLetter() ? c.toLower().unicode() : u'\0';
		}

		return built;
	}();

	return table;
}

// Null for anything that is not a letter: no letter lowers to null, so one lookup replaces isLetter() and toLower().
[[nodiscard]] inline QChar loweredLetter(QChar c, const LoweredLetters& table) noexcept
{
	const char16_t code = c.unicode();
	if (code < tabulatedCodePoints)
		return QChar{ table[code] };

	return c.isLetter() ? c.toLower() : QChar{};
}

}

// Counts a trigram at every letter, non-letters dropping out without breaking the window: a trigram spans a word boundary.
// The baked frequency tables were built by this same function, so a text scored against them must be parsed the same way.
bool CTextParser::parse(const QString& text)
{
	const QChar* const textChars = text.constData();
	const qsizetype textSize = text.size();
	const LoweredLetters& letterTable = loweredLetterTable(); // One initialization guard per call instead of per character

	// Null characters: the sliding window is short of letters until all three have shifted in, and no letter is null
	OccurrenceTable::Trigram trigram{};
	quint64 trigramsCount = 0;

	for (qsizetype i = 0; i < textSize; ++i)
	{
		const QChar lowered = loweredLetter(textChars[i], letterTable);
		if (lowered.isNull())
			continue;

		trigram.chars[0] = trigram.chars[1];
		trigram.chars[1] = trigram.chars[2];
		trigram.chars[2] = lowered;

		if (trigram.chars[0].isNull()) [[unlikely]]
			continue;

		_parsingResult.trigramOccurrenceTable[trigram].rawCount += 1;
		++trigramsCount;
	}

	_parsingResult.totalTrigramsCount += trigramsCount;

	return trigramsCount > 10;
}

void CTextParser::clear()
{
	_parsingResult.trigramOccurrenceTable.clear();
	_parsingResult.totalTrigramsCount = 0;
}


const CTextParser::OccurrenceTable & CTextParser::parsingResult() const
{
	return _parsingResult;
}
