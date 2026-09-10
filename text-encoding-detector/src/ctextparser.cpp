#include "ctextparser.h"

#include <math.h>

// Counts a trigram at every letter, non-letters dropping out without breaking the window: a trigram spans a word boundary.
// The baked frequency tables were built by this same function, so a text scored against them must be parsed the same way.
bool CTextParser::parse(const QString& text)
{
	const QChar* const textChars = text.constData();
	const qsizetype textSize = text.size();

	// Null characters: the sliding window is short of letters until all three have shifted in, and no letter is null
	OccurrenceTable::Trigram trigram{};
	quint64 trigramsCount = 0;

	for (qsizetype i = 0; i < textSize; ++i)
	{
		const QChar c = textChars[i];
		if (!c.isLetter())
			continue;

		trigram.chars[0] = trigram.chars[1];
		trigram.chars[1] = trigram.chars[2];
		trigram.chars[2] = c.toLower();

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

void CTextParser::calculateLoss() noexcept
{
	for (auto& pair : _parsingResult.trigramOccurrenceTable)
	{
		auto& stats = pair.second;
		stats.loss = -logf((float)stats.rawCount / (float)_parsingResult.totalTrigramsCount);
	}
}


const CTextParser::OccurrenceTable & CTextParser::parsingResult() const
{
	return _parsingResult;
}
