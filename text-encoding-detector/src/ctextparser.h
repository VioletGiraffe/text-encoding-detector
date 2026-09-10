#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <boost/unordered/unordered_flat_map.hpp>

#include <array>
#include <cstdint>
#include <type_traits>

class CTextParser
{
public:
	struct OccurrenceTable
	{
		// Three code points in the low 48 bits, oldest first: one integer to compare, and a few instructions to hash
		struct Trigram {
			constexpr Trigram() noexcept = default;
			constexpr Trigram(QChar first, QChar second, QChar third) noexcept
				: packed{ uint64_t{ first.unicode() } | (uint64_t{ second.unicode() } << 16) | (uint64_t{ third.unicode() } << 32) }
			{}

			[[nodiscard]] inline QString toString() const {
				const std::array<QChar, 3> chars{ characterAt(0), characterAt(1), characterAt(2) };
				return QString(chars.data(), chars.size());
			}

			inline bool constexpr operator==(const Trigram& other) const noexcept = default;

			// Drops the oldest character and appends c
			inline constexpr void shiftIn(QChar c) noexcept {
				packed = (packed >> 16) | (uint64_t{ c.unicode() } << 32);
			}

			// False until three characters have shifted in: the oldest slot is null until then, and no letter is null
			[[nodiscard]] inline constexpr bool isComplete() const noexcept {
				return (packed & 0xFFFFULL) != 0;
			}

			// An all-ASCII trigram decodes the same under every 8-bit codec and so cannot tell them apart
			[[nodiscard]] inline constexpr bool hasNonAsciiCharacter() const noexcept {
				return (packed & 0x0000FF80FF80FF80ULL) != 0;
			}

			uint64_t packed = 0;

		private:
			[[nodiscard]] inline constexpr QChar characterAt(int index) const noexcept {
				return QChar{ static_cast<char16_t>(packed >> (index * 16)) };
			}
		};

		struct Stats {
			quint64 rawCount = 0;
		};

		struct HashTrigram {
			using is_avalanching = std::true_type;

			// splitmix64's finalizer, which avalanches a key this small in a handful of instructions
			[[nodiscard]] inline constexpr uint64_t operator()(const Trigram& trigram) const noexcept {
				uint64_t key = trigram.packed;
				key ^= key >> 30;
				key *= 0xbf58476d1ce4e5b9ULL;
				key ^= key >> 27;
				key *= 0x94d049bb133111ebULL;
				return key ^ (key >> 31);
			}
		};

		boost::unordered_flat_map<Trigram, Stats, HashTrigram> trigramOccurrenceTable;
		quint64 totalTrigramsCount = 0;
	};

	// Subsequent calls to parse() will not reset the frequency table
	bool parse(const QString& text);

	// This method clears the table and sets counters to 0
	void clear();

	[[nodiscard]] const OccurrenceTable& parsingResult() const;

private:
	OccurrenceTable _parsingResult;
};
