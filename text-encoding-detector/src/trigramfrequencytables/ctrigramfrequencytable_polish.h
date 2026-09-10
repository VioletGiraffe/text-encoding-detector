#pragma once

#include "ctrigramfrequencytable_base.h"

class CTrigramFrequencyTable_Polish final : public CTrigramFrequencyTable_Base
{
public:
	CTrigramFrequencyTable_Polish() noexcept;

	[[nodiscard]] inline QString language() const override { return "Polish"; }
};
