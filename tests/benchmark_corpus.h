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

// ASCII files the study sprinkles the languages into; test-only, and no table is built from them
enum class Host { Code, Json };

// A work by an author the language's table was not built from: the table's generalization, where the test
// prefix of the work it was built from shows its fit. Test-only.
enum class OtherAuthor { Kuprin };

inline constexpr OtherAuthor allOtherAuthors[] = { OtherAuthor::Kuprin };

[[nodiscard]] const char* name(Language language);
[[nodiscard]] const char* name(Host host);
[[nodiscard]] const char* name(OtherAuthor author);
[[nodiscard]] Language language(OtherAuthor author);

// The 8-bit codecs this language's text is lossless in, and which detection therefore has to choose between
[[nodiscard]] std::vector<const char*> codecNames(Language language);

// Decoded on first use and kept. Empty only if the committed file is missing, which the tests fail on.
[[nodiscard]] const QString& text(Language language);
[[nodiscard]] const QString& text(Host host);
[[nodiscard]] const QString& text(OtherAuthor author);

// The first 'characters' of 'text', less a trailing lone surrogate: one encodes as a replacement character.
// Sliced before it is encoded, which keeps every multi-byte sequence whole - a cut inside one makes the slice
// invalid UTF-8, and that is a different input class altogether.
[[nodiscard]] QString slice(const QString& text, qsizetype characters);
[[nodiscard]] QString slice(Language language, qsizetype characters);

// 'text' encoded as 'codecName', with no byte order mark: a BOM would send the detector down its BOM shortcut
// instead of the path under measurement
[[nodiscard]] QByteArray encoded(const QString& text, const char* codecName);
[[nodiscard]] QByteArray encoded(Language language, const char* codecName, qsizetype characters); // Of slice()

// As much of 'text' as 'codecName' can carry: equal to it for the codecs codecNames() lists
[[nodiscard]] QString representable(const QString& text, const char* codecName);
[[nodiscard]] QString representable(Language language, const char* codecName, qsizetype characters); // Of slice()

// The first 'bytes' bytes of the test executable: binary input of the shape a file viewer meets in the wild
[[nodiscard]] QByteArray executableBytes(qsizetype bytes);

}
