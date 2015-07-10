#include "ctextparser.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QTextStream>
#include <QFile>
#include <QBuffer>
RESTORE_COMPILER_WARNINGS

#include <random>

CTextParser::CTextParser()
{
}

bool CTextParser::parse(const QString & textFilePath, const QString& codecName, size_t sampleSize)
{
	QFile file(textFilePath);
	if (!file.exists())
		return false;

	return parse(file, codecName, sampleSize);
}

bool CTextParser::parse(const QByteArray & textData, const QString& codecName, size_t sampleSize)
{
	QBuffer buffer(const_cast<QByteArray*>(&textData));
	return parse(buffer, codecName, sampleSize);
}

bool CTextParser::parse(QIODevice & textDevice, const QString& codecName, size_t sampleSize)
{
	assert_r(!codecName.isEmpty());
	assert_and_return_r(textDevice.isOpen() || textDevice.open(QIODevice::ReadOnly), false);

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

	assert_r(currentTrigram.length() == 3);
	++_parsingResult.trigramOccurrenceTable[currentTrigram];
	++_parsingResult.totalTrigrammsCount;

	if (sampleSize > 0 && sampleSize < (size_t)textDevice.size() - 3)
	{
		std::mt19937 generator;
		generator.seed((unsigned long)(textDevice.size()));
		std::uniform_int_distribution<qint64> distribution(3, textDevice.size() - sampleSize - 3);
		const auto randomBlockOffset = distribution(generator);
		const bool succ = stream.seek(randomBlockOffset);
		assert_r(succ);
	}
	else
		sampleSize = 0;

	size_t symbolCounter = 0;
	while ((sampleSize == 0 && !stream.atEnd()) || (sampleSize > 0 && symbolCounter < sampleSize))
	{
		stream >> ch;
		++symbolCounter;

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
