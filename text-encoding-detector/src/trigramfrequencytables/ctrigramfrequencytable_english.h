#pragma once

#include "ctrigramfrequencytable_base.h"

class CTrigramFrequencyTable_English : public CTrigramFrequencyTable_Base
{
public:
	CTrigramFrequencyTable_English() noexcept;

	[[nodiscard]] inline QString language() const override { return "English"; }
};
