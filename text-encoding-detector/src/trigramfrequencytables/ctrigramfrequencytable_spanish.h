#pragma once

#include "ctrigramfrequencytable_base.h"

class CTrigramFrequencyTable_Spanish final : public CTrigramFrequencyTable_Base
{
public:
	CTrigramFrequencyTable_Spanish() noexcept;

	[[nodiscard]] inline QString language() const override { return "Spanish"; }
};
