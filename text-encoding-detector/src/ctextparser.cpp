#include "ctextparser.h"

#include <QTextStream>
#include <QFile>
#include <QBuffer>

#include <assert.h>

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
	assert(!codecName.isEmpty());
	if (!textDevice.isOpen() && !textDevice.open(QIODevice::ReadOnly))
	{
		assert(!textDevice.open(QIODevice::ReadOnly));
		return false;
	}

	QTextStream stream(&textDevice);
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

	assert(currentTrigram.length() == 3);
	++_parsingResult.trigramOccurrenceTable[currentTrigram];
	++_parsingResult.totalTrigrammsCount;

	while (!stream.atEnd())
	{
		stream >> ch;

		if (ch.isLetter())
		{
			ch = ch.toLower();
			currentTrigram.remove(0, 1);
			currentTrigram.append(ch);

			++_parsingResult.trigramOccurrenceTable[currentTrigram];
			++_parsingResult.totalTrigrammsCount;
		}
	}

	return true;
}

void CTextParser::clear()
{
	_parsingResult.trigramOccurrenceTable.clear();
	_parsingResult.totalTrigrammsCount = 0;
}

const CTextParser::OccurrenceTable & CTextParser::parsingResult() const
{
	return _parsingResult;
}
