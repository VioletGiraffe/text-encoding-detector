#include "mixed_content_scenarios.h"

#include "benchmark_corpus.h"

#include <algorithm>
#include <random>
#include <ranges>
#include <string>
#include <vector>

namespace {

constexpr qsizetype interleavedBlockCharacters = 400;
constexpr qsizetype lineCharacters = 60;
constexpr double foreignShares[] = { 0.20, 0.05, 0.01 };

// One contiguous run of prose in a file that is otherwise the host, at three fifths of the way in
[[nodiscard]] QString clustered(const QString& prose, const QString& host, double proseShare, qsizetype length)
{
	const qsizetype proseCharacters = (qsizetype)(length * proseShare);
	const qsizetype start = (length - proseCharacters) * 3 / 5;

	QString mixed = host.left(start);
	mixed += prose.left(proseCharacters);
	mixed += host.mid(start, length - mixed.size());

	return mixed;
}

// Paragraph-sized blocks of prose scattered through the host
[[nodiscard]] QString interleaved(const QString& prose, const QString& host, double proseShare, qsizetype length)
{
	std::mt19937 rng{ 20260910u };
	std::uniform_real_distribution<double> draw{ 0.0, 1.0 };

	QString mixed;
	mixed.reserve(length);

	qsizetype proseRead = 0;
	qsizetype hostRead = 0;
	while (mixed.size() < length)
	{
		const qsizetype before = mixed.size();

		if (draw(rng) < proseShare)
		{
			mixed += prose.mid(proseRead, interleavedBlockCharacters);
			proseRead += interleavedBlockCharacters;
		}
		else
		{
			mixed += host.mid(hostRead, interleavedBlockCharacters);
			hostRead += interleavedBlockCharacters;
		}

		if (mixed.size() == before) // Both sources exhausted, which no share in use here reaches
			break;
	}

	return mixed.left(length);
}

// Single lines of prose among the host's own lines, the way a source file carries comments and a log carries
// messages; the prose is kept at its share of the characters written so far
[[nodiscard]] QString lined(const QString& prose, const QString& host, double proseShare, qsizetype length)
{
	QString mixed;
	mixed.reserve(length);

	qsizetype proseRead = 0;
	qsizetype hostRead = 0;
	qsizetype proseWritten = 0;
	while (mixed.size() < length)
	{
		const qsizetype before = mixed.size();

		if (static_cast<double>(proseWritten) < proseShare * static_cast<double>(mixed.size()))
		{
			QString line = prose.mid(proseRead, lineCharacters);
			proseRead += lineCharacters;
			line.replace(QChar{ '\n' }, QChar{ ' ' });
			line += QChar{ '\n' };

			mixed += line;
			proseWritten += line.size();
		}
		else
		{
			const qsizetype newline = host.indexOf(QChar{ '\n' }, hostRead);
			const qsizetype lineEnd = newline < 0 ? host.size() : newline + 1;
			mixed += host.mid(hostRead, lineEnd - hostRead);
			hostRead = lineEnd;
		}

		if (mixed.size() == before)
			break;
	}

	return mixed.left(length);
}

}

std::vector<MixedContent::Scenario> MixedContent::scenarios()
{
	struct HostText { const char* name; QString text; };
	const HostText hosts[] = {
		{ "prose", BenchmarkCorpus::text(BenchmarkCorpus::Language::English).left(scenarioCharacters) },
		{ BenchmarkCorpus::name(BenchmarkCorpus::Host::Code), BenchmarkCorpus::text(BenchmarkCorpus::Host::Code).left(scenarioCharacters) },
		{ BenchmarkCorpus::name(BenchmarkCorpus::Host::Json), BenchmarkCorpus::text(BenchmarkCorpus::Host::Json).left(scenarioCharacters) },
	};

	constexpr double largestShare = *std::ranges::max_element(foreignShares);

	std::vector<Scenario> all;
	for (const BenchmarkCorpus::Language language : BenchmarkCorpus::nonAsciiLanguages)
	{
		const std::string name = BenchmarkCorpus::name(language);
		const std::vector<const char*> codecs = BenchmarkCorpus::codecNames(language);
		const QString prose = BenchmarkCorpus::text(language).left(scenarioCharacters);
		const qsizetype length = std::min(scenarioCharacters, static_cast<qsizetype>(static_cast<double>(prose.size()) / largestShare));

		all.push_back(Scenario{ name + " prose", prose, codecs });

		for (const HostText& host : hosts)
		{
			for (const double share : foreignShares)
			{
				const std::string label = name + " " + std::to_string((int)(share * 100)) + "% " + host.name;
				all.push_back(Scenario{ label + " clustered", clustered(prose, host.text, share, length), codecs });
				all.push_back(Scenario{ label + " interleaved", interleaved(prose, host.text, share, length), codecs });
				all.push_back(Scenario{ label + " lines", lined(prose, host.text, share, length), codecs });
			}
		}
	}

	return all;
}

