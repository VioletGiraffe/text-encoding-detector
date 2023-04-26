#pragma once

#include "../ctextparser.h"

class CTrigramFrequencyTable_Base
{
public:
	virtual ~CTrigramFrequencyTable_Base() = default;

	[[nodiscard]] inline const CTextParser::OccurrenceTable& trigramOccurrenceTable() const {return _table;}

	[[nodiscard]] virtual QString language() const = 0;

protected:
	CTextParser::OccurrenceTable _table;
};
