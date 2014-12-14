#pragma once

#include "ctrigramfrequencytable_base.h"

#include <QString>
#include <map>

class CTrigramFrequencyTable_Russian : public CTrigramFrequencyTable_Base
{
public:
	CTrigramFrequencyTable_Russian();

	inline QString language() const override {return "Russian";}
};
