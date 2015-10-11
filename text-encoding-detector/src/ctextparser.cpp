#include "ctextparser.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QTextStream>
#include <QFile>
#include <QBuffer>
RESTORE_COMPILER_WARNINGS

CTextParser::CTextParser()
{
}

bool CTextParser::parse(const QString & textFilePath, const QString& codecName)
{
	QFile file(textFilePath);
	if (!file.exists())
		return false;

	return parse(file, codecName);
}

bool CTextParser::parse(const QByteArray & textData, const QString& codecName)
{
	QBuffer buffer(const_cast<QByteArray*>(&textData));
	return parse(buffer, codecName);
}

bool CTextParser::parse(QIODevice & textDevice, const QString& codecName)
{
	assert_r(!codecName.isEmpty());
	assert_and_return_r(textDevice.isOpen() || textDevice.open(QIODevice::ReadOnly), false);

	QTextStream stream(&textDevice);
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

	const qint64 stride = textDevice.size() <= numCharactersToAnalyze || numChunks == 0 ? 0 : (textDevice.size() - numCharactersToAnalyze) / (numChunks - 1);
	qint64 charactersCounter = 0;
	while (!stream.atEnd())
	{
		stream >> ch;
		++charactersCounter;
		if (charactersCounter > chunkSize)
		{
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
