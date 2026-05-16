#pragma once

#include "ctrigramfrequencytable_base.h"

class CTrigramFrequencyTable_Russian final : public CTrigramFrequencyTable_Base
{
public:
	CTrigramFrequencyTable_Russian() noexcept;

	[[nodiscard]] inline QString language() const override { return "Russian"; }
};
