#pragma once

#include "ctrigramfrequencytable_base.h"

class CTrigramFrequencyTable_English : public CTrigramFrequencyTable_Base
{
public:
	CTrigramFrequencyTable_English();

	inline QString language() const override {return "English";}
};
