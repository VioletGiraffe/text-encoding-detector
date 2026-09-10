#pragma once

#include "compiler/compiler_warnings_control.h"
#include <hash/wheathash.hpp>

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <boost/unordered/unordered_flat_map.hpp>

#include <array>
#include <type_traits>

class CTextParser
{
public:
	struct OccurrenceTable
	{
		struct Trigram {
			[[nodiscard]] inline QString toString() const {
				return QString(chars.data(), chars.size());
			}

			inline bool constexpr operator==(const Trigram& other) const noexcept = default;

			// An all-ASCII trigram decodes the same under every 8-bit codec and so cannot tell them apart
			[[nodiscard]] inline bool hasNonAsciiCharacter() const noexcept {
				return chars[0].unicode() >= 0x80 || chars[1].unicode() >= 0x80 || chars[2].unicode() >= 0x80;
			}

			std::array<QChar, 3> chars;
		};

		struct Stats {
			quint64 rawCount = 0;
			float loss = 0.0f;
		};

		struct HashTrigram {
			using is_avalanching = std::true_type;

			[[nodiscard]] inline uint64_t operator()(const Trigram& t) const noexcept {
				return ::wheathash64(t.chars.data(), t.chars.size() * sizeof(t.chars[0]));
			}
		};

		boost::unordered_flat_map<Trigram, Stats, HashTrigram> trigramOccurrenceTable;
		quint64 totalTrigramsCount = 0;
	};

	// Subsequent calls to parse() will not reset the frequency table
	bool parse(const QString& text);

	// This method clears the table and sets counters to 0
	void clear();

	void calculateLoss() noexcept;
	[[nodiscard]] const OccurrenceTable& parsingResult() const;

private:
	OccurrenceTable _parsingResult;
};
