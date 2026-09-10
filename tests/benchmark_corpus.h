#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QByteArray>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <string>
#include <vector>

// The test half of the committed corpus in ../corpus: real prose in six languages, each verified to survive
// the 8-bit codecs listed for it without losing a character, and none of it seen by the trigram tables.
// See ../corpus/README.md for sources and preparation.
namespace BenchmarkCorpus {

enum class Language { English, French, German, Spanish, Polish, Russian };

inline constexpr Language allLanguages[] = {
	Language::English, Language::French, Language::German, Language::Spanish, Language::Polish, Language::Russian
};

// Every language but English, which carries no non-ASCII character and so has no encoding to detect
inline constexpr Language nonAsciiLanguages[] = {
	Language::French, Language::German, Language::Spanish, Language::Polish, Language::Russian
};

[[nodiscard]] const char* name(Language language);

// The 8-bit codecs this language's text is lossless in, and which detection therefore has to choose between
[[nodiscard]] std::vector<const char*> codecNames(Language language);

// Decoded on first use and kept. Empty only if the committed file is missing, which the tests fail on.
[[nodiscard]] const QString& text(Language language);

// What encoded() encodes: the first 'characters' of text(), less a trailing lone surrogate
[[nodiscard]] QString slice(Language language, qsizetype characters);

// slice() encoded as 'codecName', with no byte order mark: a BOM would send the detector down its BOM
// shortcut instead of the path under measurement.
// The text is sliced before it is encoded, which keeps every multi-byte sequence whole - a cut inside one
// makes the slice invalid UTF-8, and that is a different input class altogether.
[[nodiscard]] QByteArray encoded(Language language, const char* codecName, qsizetype characters);

// As much of slice() as 'codecName' can carry. Equal to slice() for the codecs codecNames() lists, and the
// two part only where a caller asks for some other codec.
[[nodiscard]] QString representable(Language language, const char* codecName, qsizetype characters);

// The first 'bytes' bytes of the test executable: binary input of the shape a file viewer meets in the wild
[[nodiscard]] QByteArray executableBytes(qsizetype bytes);

}
