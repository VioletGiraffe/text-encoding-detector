#pragma once

#include "ctrigramfrequencytable_base.h"

class CTrigramFrequencyTable_English final : public CTrigramFrequencyTable_Base
{
public:
	CTrigramFrequencyTable_English();

	[[nodiscard]] inline QString language() const override {return QStringLiteral("English");}
};
