#include "ctextparser.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QTextStream>
#include <QFile>
RESTORE_COMPILER_WARNINGS

bool CTextParser::parse(const QString & textFilePath, const QString& codecName)
{
	QFile file(textFilePath);
	if (!file.exists())
		return false;

	// TODO: avoid reading the whole file. Currently (as of Qt 5.13.2) not easily doable due to bugs in QBuffer (seek(), bytesAvailable() behave in a weird way).
	return parse(file.readAll(), codecName);
}

bool CTextParser::parse(QIODevice& textDevice, const QString& codecName)
{
	// TODO: avoid reading the whole file. Currently (as of Qt 5.13.2) not easily doable due to bugs in QBuffer (seek(), bytesAvailable() behave in a weird way).
	return parse(textDevice.readAll(), codecName);
}

bool CTextParser::parse(const QByteArray& textData, const QString& codecName)
{
	assert_r(!codecName.isEmpty());

	QTextStream stream(const_cast<QByteArray*>(std::addressof(textData)));
	stream.setAutoDetectUnicode(false);
	stream.setCodec(codecName.toUtf8().data());

	// Read the first 3 symbols
	QString currentTrigram;
	QChar ch;
	for (int i = 0; i < 3;)
	{
		if (stream.atEnd())
			return false;

		stream >> ch;
		if (ch.isLetter())
		{
			ch = ch.toLower();
			currentTrigram.append(ch);
			++i;
		}
	}

	assert_r(currentTrigram.length() == 3);
	++_parsingResult.trigramOccurrenceTable[currentTrigram];
	++_parsingResult.totalTrigramsCount;

	const qint64 numCharactersToAnalyze = 10000;
	const qint64 numChunks = 10, chunkSize = numCharactersToAnalyze / numChunks;
	static_assert(numCharactersToAnalyze > 0 && numChunks > 0, "Number of characters and number of chunks must be greater than 0");

	// Reading up to numCharactersToAnalyze characters, in numChunks x (numCharactersToAnalyze/numChunks) evenly spaced chunks

	const qint64 stride = textData.size() <= numCharactersToAnalyze || numChunks == 0 ? 0 : (textData.size() - numCharactersToAnalyze) / (numChunks - 1);
	qint64 charactersCounter = 0;
	while (!stream.atEnd())
	{
		stream >> ch;
		++charactersCounter;
		if (charactersCounter > chunkSize)
		{
			//auto newPos = std::min(stream.pos() + stride, textData.size());
			if (stream.seek(stream.pos() + stride))
			{
				charactersCounter = 0;
				continue;
			}
			else
				break; // seek fails when we're trying to move past the end, which is our cue to stop
		}

		currentTrigram.remove(0, 1);
		currentTrigram.append(ch.toLower());


		++_parsingResult.trigramOccurrenceTable[currentTrigram];
		++_parsingResult.totalTrigramsCount;
	}

	return true;
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
