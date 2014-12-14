#pragma once

#include "ctrigramfrequencytable_base.h"

#include <QString>
#include <map>

class CTrigramFrequencyTable_English : public CTrigramFrequencyTable_Base
{
public:
	CTrigramFrequencyTable_English();

	inline QString language() const override {return "English";}
};
