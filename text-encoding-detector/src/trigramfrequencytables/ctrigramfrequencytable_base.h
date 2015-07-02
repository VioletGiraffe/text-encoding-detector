#pragma once

#include "../ctextparser.h"

class CTrigramFrequencyTable_Base
{
public:
	inline const CTextParser::OccurrenceTable& trigramOccurrenceTable() const {return _table;}

	virtual QString language() const = 0;

protected:
	CTextParser::OccurrenceTable _table;
};
