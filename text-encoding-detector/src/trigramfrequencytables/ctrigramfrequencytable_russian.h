#pragma once

#include "ctrigramfrequencytable_base.h"

class CTrigramFrequencyTable_Russian final : public CTrigramFrequencyTable_Base
{
public:
	CTrigramFrequencyTable_Russian();

	[[nodiscard]] inline QString language() const override {return QStringLiteral("Russian");}
};
