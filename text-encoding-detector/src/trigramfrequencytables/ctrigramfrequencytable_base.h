#pragma once

#include "../ctextparser.h"

#include <utility>

class CTrigramFrequencyTable_Base
{
public:
	explicit CTrigramFrequencyTable_Base(CTextParser::OccurrenceTable&& table) noexcept
		: _table{ std::move(table) }, _countsNormSquared{ sumOfSquaredCounts(_table) }
	{
	}

	virtual ~CTrigramFrequencyTable_Base() = default;

	[[nodiscard]] const CTextParser::OccurrenceTable& trigramOccurrenceTable() const noexcept { return _table; }

	// The model's half of a cosine similarity, over the trigrams scoring considers: those with a non-ASCII character.
	// A constant of the table, and the tables are large.
	[[nodiscard]] double countsNormSquared() const noexcept { return _countsNormSquared; }

	[[nodiscard]] virtual QString language() const = 0;

private:
	[[nodiscard]] static double sumOfSquaredCounts(const CTextParser::OccurrenceTable& table) noexcept
	{
		double sum = 0.0;
		for (const auto& [trigram, stats] : table.trigramOccurrenceTable)
		{
			if (!trigram.hasNonAsciiCharacter())
				continue;

			const double count = static_cast<double>(stats.rawCount);
			sum += count * count;
		}

		return sum;
	}

	const CTextParser::OccurrenceTable _table;
	const double _countsNormSquared;
};
