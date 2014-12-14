#ifndef CTEXTPARSER_H
#define CTEXTPARSER_H

#include <QString>

#include <map>

class QByteArray;
class QIODevice;

class CTextParser
{
public:
	CTextParser();

	struct OccurrenceTable
	{
		OccurrenceTable(): totalTrigrammsCount(0) {}
		std::map<QString /*trigraph*/, quint64 /*count*/> trigramOccurrenceTable;
		quint64 totalTrigrammsCount;
	};

	// Subsequent calls to parse() will not reset the frequency table
	bool parse(const QString& textFilePath, const QString& codecName);
	bool parse(const QByteArray& textData, const QString& codecName);
	bool parse(QIODevice& textDevice, const QString& codecName);

	// This method clears the table and sets counters to 0
	void clear();

	const OccurrenceTable& parsingResult() const;

private:
	OccurrenceTable _parsingResult;
};

#endif // CTEXTPARSER_H
