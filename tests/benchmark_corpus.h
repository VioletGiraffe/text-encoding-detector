#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QByteArray>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <string>

// The benchmarks run on a real text of several MB, which is too large to keep in the repository. Its path and
// its encoding are build-time defaults pointing at the analyzer's corpus folder, both overridable through the
// environment: TEXT_ENCODING_DETECTOR_CORPUS and TEXT_ENCODING_DETECTOR_CORPUS_CODEC.
// Where the file is absent, text() is empty and every case that needs it reports itself skipped.
namespace BenchmarkCorpus {

// Decoded on the first call and kept. Empty where the file is unreadable or the codec unknown.
[[nodiscard]] const QString& text();

[[nodiscard]] QString filePath();
[[nodiscard]] QByteArray codecName();

// Names the file that was looked for and the two variables that point elsewhere
[[nodiscard]] std::string missingCorpusMessage();

// What encoded() encodes: the first 'characters' of text(), less a trailing lone surrogate
[[nodiscard]] QString slice(qsizetype characters);

// slice() encoded as 'targetCodecName', with no byte order mark: a BOM would send the detector down its BOM
// shortcut instead of the path under measurement.
// The text is sliced before it is encoded, which keeps every multi-byte sequence whole - a cut inside one
// makes the slice invalid UTF-8, and that is a different input class altogether.
[[nodiscard]] QByteArray encoded(const char* targetCodecName, qsizetype characters);

// The first 'bytes' bytes of the test executable: binary input of the shape a file viewer meets in the wild
[[nodiscard]] QByteArray executableBytes(qsizetype bytes);

}
