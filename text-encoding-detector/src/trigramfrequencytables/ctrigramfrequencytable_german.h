#pragma once

#include "ctrigramfrequencytable_base.h"

class CTrigramFrequencyTable_German final : public CTrigramFrequencyTable_Base
{
public:
	CTrigramFrequencyTable_German() noexcept;

	[[nodiscard]] inline QString language() const override { return "German"; }
};
