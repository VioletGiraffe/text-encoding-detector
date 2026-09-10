#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "benchmark_corpus.h"

#include "ctextparser.h"
#include "trigramfrequencytables/ctrigramfrequencytable_russian.h"

#include "container/flat_map.hpp"
#include <hash/wheathash.hpp>

DISABLE_COMPILER_WARNINGS
#include <3rdparty/ankerl/unordered_dense.h>
#include <boost/unordered/unordered_flat_map.hpp>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <string>
#include <type_traits>
#include <vector>

// The trigram table is the detector's one data structure, and it carries two workloads that stress a container
// in opposite ways:
//   build  - insert-or-increment, once per letter, growing to tens of thousands of distinct keys (CTextParser::parse)
//   lookup - find(), once per distinct sample key, against a model table that never changes (cosineDistance)
// Both run once per codec, and detect() tries a dozen.
//
// Two key shapes are measured. The uint64 packing three code points into 48 bits is what the library holds: the
// comparison is a single integer one, and a few instructions replace a call into a general-purpose hash. Three
// QChars compared as six bytes is the shape it replaced, kept here so the choice can be rechecked.

namespace {

using Stats = CTextParser::OccurrenceTable::Stats;

struct CharsTrigram
{
	bool constexpr operator==(const CharsTrigram& other) const noexcept = default;

	std::array<QChar, 3> chars;
};

struct HashCharsTrigram
{
	using is_avalanching = std::true_type;

	[[nodiscard]] uint64_t operator()(const CharsTrigram& trigram) const noexcept
	{
		return ::wheathash64(trigram.chars.data(), trigram.chars.size() * sizeof(trigram.chars[0]));
	}
};

// What CTextParser::OccurrenceTable::HashTrigram does, over the same key
struct HashPackedTrigram
{
	using is_avalanching = std::true_type;

	[[nodiscard]] uint64_t operator()(uint64_t key) const noexcept
	{
		key ^= key >> 30;
		key *= 0xbf58476d1ce4e5b9ULL;
		key ^= key >> 27;
		key *= 0x94d049bb133111ebULL;
		return key ^ (key >> 31);
	}
};

[[nodiscard]] constexpr uint64_t packed(const CharsTrigram& trigram) noexcept
{
	return uint64_t{ trigram.chars[0].unicode() }
		| (uint64_t{ trigram.chars[1].unicode() } << 16)
		| (uint64_t{ trigram.chars[2].unicode() } << 32);
}

[[nodiscard]] constexpr CharsTrigram unpacked(uint64_t key) noexcept
{
	return CharsTrigram{ { QChar{ static_cast<char16_t>(key) }, QChar{ static_cast<char16_t>(key >> 16) }, QChar{ static_cast<char16_t>(key >> 32) } } };
}

template <typename Key> using HashFor = std::conditional_t<std::is_same_v<Key, uint64_t>, HashPackedTrigram, HashCharsTrigram>;

template <typename Key> using BoostMap = boost::unordered_flat_map<Key, Stats, HashFor<Key>>;
template <typename Key> using AnkerlMap = ankerl::unordered_dense::map<Key, Stats, HashFor<Key>>;
using SortedVectorMap = flat_map<uint64_t, Stats>;

// Russian: the trigram stream a real detection pass walks, and the language the baked model below is for
constexpr auto corpusLanguage = BenchmarkCorpus::Language::Russian;

constexpr qsizetype characterCounts[] = { 64 * 1024, 256 * 1024, 768 * 1024 };

// A sorted vector shifts its tail on every key it has not seen before, so its build is quadratic in the
// distinct key count while the hash maps stay linear. The ceiling keeps one comparable point without
// spending minutes on the case that is already decided.
constexpr qsizetype maxCharactersForSortedVectorBuild = 64 * 1024;

template <typename Map> constexpr bool isSortedVector = std::is_same_v<Map, SortedVectorMap>;

// The trigram stream parse() walks: one entry per letter from the third on, duplicates and all
[[nodiscard]] std::vector<CharsTrigram> trigramStream(qsizetype characters)
{
	std::vector<CharsTrigram> stream;

	const QString text = BenchmarkCorpus::text(corpusLanguage).left(characters);
	CharsTrigram trigram{};
	for (const QChar c : text)
	{
		if (!c.isLetter())
			continue;

		trigram.chars[0] = trigram.chars[1];
		trigram.chars[1] = trigram.chars[2];
		trigram.chars[2] = c.toLower();

		if (!trigram.chars[0].isNull())
			stream.push_back(trigram);
	}

	return stream;
}

[[nodiscard]] std::vector<uint64_t> packedStream(qsizetype characters)
{
	const std::vector<CharsTrigram> source = trigramStream(characters);

	std::vector<uint64_t> stream;
	stream.reserve(source.size());
	for (const CharsTrigram& trigram : source)
		stream.push_back(packed(trigram));

	return stream;
}

template <typename Key>
[[nodiscard]] std::vector<Key> streamOf(qsizetype characters)
{
	if constexpr (std::is_same_v<Key, uint64_t>)
		return packedStream(characters);
	else
		return trigramStream(characters);
}

// The keys of the baked Russian table, which is what cosineDistance() looks a sample up in
template <typename Key>
[[nodiscard]] std::vector<Key> modelKeys()
{
	const CTrigramFrequencyTable_Russian table;
	const auto& model = table.trigramOccurrenceTable().trigramOccurrenceTable;

	std::vector<Key> keys;
	keys.reserve(model.size());
	for (const auto& [trigram, stats] : model)
	{
		if constexpr (std::is_same_v<Key, uint64_t>)
			keys.push_back(trigram.packed);
		else
			keys.push_back(unpacked(trigram.packed));
	}

	return keys;
}

template <typename Map>
void fill(Map& map, const std::vector<typename Map::key_type>& keys)
{
	for (const auto& key : keys)
		map[key].rawCount += 1;
}

// An order over either key shape, so a key set can be reduced to its distinct members without a container
template <typename Key>
[[nodiscard]] uint64_t orderingValue(const Key& key) noexcept
{
	if constexpr (std::is_same_v<Key, uint64_t>)
		return key;
	else
		return packed(key);
}

template <typename Key>
[[nodiscard]] std::vector<Key> distinctKeysOf(std::vector<Key> keys)
{
	std::ranges::sort(keys, {}, orderingValue<Key>);
	const auto duplicates = std::ranges::unique(keys, {}, orderingValue<Key>);
	keys.erase(duplicates.begin(), duplicates.end());

	return keys;
}

[[nodiscard]] std::string caseName(const char* container, const char* key, qsizetype characters)
{
	return std::string{ container } + ", " + key + ", " + std::to_string(characters / 1024) + "K chars";
}

template <typename Map>
void benchmarkBuild(const char* container, const char* key)
{
	using Key = typename Map::key_type;

	for (const qsizetype characters : characterCounts)
	{
		if (BenchmarkCorpus::text(corpusLanguage).size() < characters)
			continue;

		if constexpr (isSortedVector<Map>)
		{
			if (characters > maxCharactersForSortedVectorBuild)
				continue;
		}

		const std::vector<Key> keys = streamOf<Key>(characters);
		BENCHMARK_ADVANCED(caseName(container, key, characters))(Catch::Benchmark::Chronometer meter)
		{
			// Construction and destruction are inside the region: parse() pays both, once per codec
			meter.measure([&keys] {
				Map map;
				fill(map, keys);
				return map.size();
			});
		};
	}
}

template <typename Map>
void benchmarkLookup(const char* container, const char* key)
{
	using Key = typename Map::key_type;

	for (const qsizetype characters : characterCounts)
	{
		if (BenchmarkCorpus::text(corpusLanguage).size() < characters)
			continue;

		Map model;
		fill(model, modelKeys<Key>());

		// The distinct keys of a sample, which is what a scoring pass walks. Reduced without a container:
		// the set is the same for every container, and building it in flat_map would cost more than the case does.
		const std::vector<Key> sampleKeys = distinctKeysOf(streamOf<Key>(characters));

		BENCHMARK_ADVANCED(caseName(container, key, characters))(Catch::Benchmark::Chronometer meter)
		{
			meter.measure([&model, &sampleKeys] {
				uint64_t found = 0;
				for (const auto& sampleKey : sampleKeys)
				{
					if (const auto it = model.find(sampleKey); it != model.end())
						found += it->second.rawCount;
				}

				return found;
			});
		};
	}
}

}

TEST_CASE("Trigram table: build", "[!benchmark]")
{
	benchmarkBuild<BoostMap<CharsTrigram>>("boost::unordered_flat_map", "3 QChars");
	benchmarkBuild<AnkerlMap<CharsTrigram>>("ankerl::unordered_dense", "3 QChars");
	benchmarkBuild<BoostMap<uint64_t>>("boost::unordered_flat_map", "uint64");
	benchmarkBuild<AnkerlMap<uint64_t>>("ankerl::unordered_dense", "uint64");
	benchmarkBuild<SortedVectorMap>("flat_map", "uint64");
}

TEST_CASE("Trigram table: build by sorting", "[!benchmark]")
{
	// What a flat container is actually good at: append every key, sort once, then count the runs.
	// The result is a sorted key array, which is the form flat_map holds and the form a lookup pass wants.
	for (const qsizetype characters : characterCounts)
	{
		if (BenchmarkCorpus::text(corpusLanguage).size() < characters)
			continue;

		const std::vector<uint64_t> keys = packedStream(characters);
		BENCHMARK_ADVANCED(caseName("sort and count runs", "uint64", characters))(Catch::Benchmark::Chronometer meter)
		{
			meter.measure([&keys] {
				std::vector<uint64_t> sorted = keys;
				std::sort(sorted.begin(), sorted.end());

				std::vector<uint64_t> distinctKeys;
				std::vector<Stats> counts;
				for (auto run = sorted.begin(); run != sorted.end();)
				{
					const auto runEnd = std::upper_bound(run, sorted.end(), *run);
					distinctKeys.push_back(*run);
					counts.push_back(Stats{ static_cast<quint64>(runEnd - run) });
					run = runEnd;
				}

				return distinctKeys.size();
			});
		};
	}
}

TEST_CASE("Trigram table: lookup against the model", "[!benchmark]")
{
	benchmarkLookup<BoostMap<CharsTrigram>>("boost::unordered_flat_map", "3 QChars");
	benchmarkLookup<AnkerlMap<CharsTrigram>>("ankerl::unordered_dense", "3 QChars");
	benchmarkLookup<BoostMap<uint64_t>>("boost::unordered_flat_map", "uint64");
	benchmarkLookup<AnkerlMap<uint64_t>>("ankerl::unordered_dense", "uint64");
	benchmarkLookup<SortedVectorMap>("flat_map", "uint64");
}
