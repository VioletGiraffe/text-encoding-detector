#include "ctextparser.h"

DISABLE_COMPILER_WARNINGS
#include <QFile>
#include <QTextCodec>
RESTORE_COMPILER_WARNINGS

#include <assert.h>

bool CTextParser::parse(const QString & textFilePath, const QString& codecName, bool fastAnalysis, bool ignoreNonLetters)
{
	QFile file(textFilePath);
	if (!file.open(QFile::ReadOnly))
		return false;

	// TODO: avoid reading the whole file. Currently (as of Qt 5.13.2) not easily doable due to bugs in QBuffer (seek(), bytesAvailable() behave in a weird way).
	return parse(file.readAll(), codecName, fastAnalysis, ignoreNonLetters);
}

bool CTextParser::parse(QIODevice& textDevice, const QString& codecName, const bool fastAnalysis, const bool ignoreNonLetters)
{
	// TODO: avoid reading the whole file. Currently (as of Qt 5.13.2) not easily doable due to bugs in QBuffer (seek(), bytesAvailable() behave in a weird way).
	return parse(textDevice.readAll(), codecName, fastAnalysis, ignoreNonLetters);
}

bool CTextParser::parse(const QByteArray& textData, const QString& codecName, const bool /*fastAnalysis*/, const bool ignoreNonLetters)
{
	assert(!codecName.isEmpty());

	QTextCodec* codec = QTextCodec::codecForName(codecName.toUtf8());
	if (!codec)
	{
		assert(codec);
		return false;
	}

	auto* decoder = codec->makeDecoder();
	const QString decodedText = decoder->toUnicode(textData);

	// Scan the text and count every trigram, ignoring non-letter characters
	QString trigramString;
	qsizetype i = 0;
	const qsizetype textSize = decodedText.size();
	// Accumulate the first 3 letters
	for (i = 0; i < textSize; ++i)
	{
		const QChar c = decodedText[i];
		bool ignore = false;
		if (ignoreNonLetters)
			ignore = !c.isLetter();
		else
			ignore = c.isPunct();

		if (ignore)
		{
			trigramString += c.toLower();
			if (trigramString.size() == 3)
				break;
		}
	}

	if (trigramString.size() < 3) [[unlikely]]
		return false;

	
	const QChar* textChars = decodedText.data();

	OccurrenceTable::Trigram trigram{ trigramString[0], trigramString[1], trigramString[2] };

	quint64 trigramsCount = 0;
	_parsingResult.trigramOccurrenceTable[trigram].rawCount += 1;
	++trigramsCount;

	for (; i < textSize; ++i)
	{
		const QChar c = textChars[i];
		if (c.isLetter())
		{
			trigram.chars[0] = trigram.chars[1];
			trigram.chars[1] = trigram.chars[2];
			trigram.chars[2] = c.toLower();

			_parsingResult.trigramOccurrenceTable[trigram].rawCount += 1;
			++trigramsCount;
		}
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
