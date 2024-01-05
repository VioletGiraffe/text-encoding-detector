#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QHash>
#include <QString>
RESTORE_COMPILER_WARNINGS

class QByteArray;
class QIODevice;

class CTextParser
{
public:
	struct OccurrenceTable
	{
		QHash<QString /*trigraph*/, quint64 /*count*/> trigramOccurrenceTable;
		quint64 totalTrigramsCount = 0;
	};

	// Subsequent calls to parse() will not reset the frequency table
	bool parse(const QString& textFilePath, const QString& codecName);
	bool parse(QIODevice& textDevice, const QString& codecName);
	bool parse(const QByteArray& textData, const QString& codecName);

	// This method clears the table and sets counters to 0
	void clear();

	[[nodiscard]] const OccurrenceTable& parsingResult() const;

private:
	OccurrenceTable _parsingResult;
};
