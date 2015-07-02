#ifndef CTEXTPARSER_H
#define CTEXTPARSER_H

#include "utils/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

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
	bool parse(const QString& textFilePath, const QString& codecName, size_t sampleSize = 0);
	bool parse(const QByteArray& textData, const QString& codecName, size_t sampleSize = 0);
	bool parse(QIODevice& textDevice, const QString& codecName, size_t sampleSize = 0);

	// This method clears the table and sets counters to 0
	void clear();

	const OccurrenceTable& parsingResult() const;

private:
	OccurrenceTable _parsingResult;
};

#endif // CTEXTPARSER_H
