#pragma once

#include "ctrigramfrequencytable_base.h"

class CTrigramFrequencyTable_French final : public CTrigramFrequencyTable_Base
{
public:
	CTrigramFrequencyTable_French() noexcept;

	[[nodiscard]] inline QString language() const override { return "French"; }
};
