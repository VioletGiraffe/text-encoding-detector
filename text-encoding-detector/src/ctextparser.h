#ifndef CTEXTPARSER_H
#define CTEXTPARSER_H

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <map>

class QByteArray;
class QIODevice;

class CTextParser
{
public:
	CTextParser() = default;

	struct OccurrenceTable
	{
		std::map<QString /*trigraph*/, quint64 /*count*/> trigramOccurrenceTable;
		quint64 totalTrigramsCount = 0;
	};

	// Subsequent calls to parse() will not reset the frequency table
	bool parse(const QString& textFilePath, const QString& codecName);
	bool parse(QIODevice& textDevice, const QString& codecName);

	// This method clears the table and sets counters to 0
	void clear();

	const OccurrenceTable& parsingResult() const;

private:
	bool parse(QByteArray& textDevice, const QString& codecName);

private:
	OccurrenceTable _parsingResult;
};

#endif // CTEXTPARSER_H
