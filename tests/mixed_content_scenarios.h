#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <string>
#include <vector>

// The corpus languages as a file viewer meets them: each whole, and each mixed into English prose, C source
// and a JSON log at three shares in three shapes. The tests assert over every one; the window study measures
// samplings of them.
namespace MixedContent {

struct Scenario
{
	std::string name;
	QString text;
	std::vector<const char*> codecNames; // The encodings this text is written in for the run
};

// The length of the English, code and json hosts; a language whose test prefix cannot fill the largest share
// of it gets shorter scenarios
constexpr qsizetype scenarioCharacters = 200000;

// The Western European languages carry their non-ASCII characters one at a time inside ASCII words, which is
// a harder shape for a sampler than Russian's runs of Cyrillic - and it is their own accent density doing it,
// not a substitution rate.
[[nodiscard]] std::vector<Scenario> scenarios();

}
