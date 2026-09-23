#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils
RESTORE_COMPILER_WARNINGS

#include "benchmark_corpus.h"

#include "ctextparser.h"
#include "trigramfrequencytables/ctrigramfrequencytable_russian.h"

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
#include <unordered_map>
#include <vector>

// The trigram table is the detector's one data structure, and it carries three workloads that stress a container
// in different ways:
//   build  - insert-or-increment into an empty table, growing to thousands of distinct keys (the first codec)
//   refill - the same after clear(), the buckets already grown (every codec after it, detect() holding one parser)
//   lookup - find(), once per distinct sample key, against a model table that never changes (cosineDistance)
// detect() tries a dozen codecs, so refill runs eleven times for each build.
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

// The same table with its values in fixed blocks instead of one vector that doubles and copies: slower on both
// workloads, so the dense map's build cost is not the growth of that vector
template <typename Key> using AnkerlSegmentedMap = ankerl::unordered_dense::segmented_map<Key, Stats, HashFor<Key>>;

// The standard's node-based table, for scale: what the detector would run on with no dependency at all
template <typename Key> using StdMap = std::unordered_map<Key, Stats, HashFor<Key>>;

// Russian: the trigram stream a real detection pass walks, and the language the baked model below is for
constexpr auto corpusLanguage = BenchmarkCorpus::Language::Russian;

constexpr qsizetype characterCounts[] = { 64 * 1024, 256 * 1024, 768 * 1024 };

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

		const std::vector<Key> keys = streamOf<Key>(characters);
		BENCHMARK_ADVANCED(caseName(container, key, characters))(Catch::Benchmark::Chronometer meter)
		{
			// Construction and destruction are inside the region: what the first codec of a pass pays, and any
			// caller that parses once. Every later codec pays the refill case instead.
			meter.measure([&keys] {
				Map map;
				fill(map, keys);
				return map.size();
			});
		};
	}
}

template <typename Map>
void benchmarkRefill(const char* container, const char* key)
{
	using Key = typename Map::key_type;

	for (const qsizetype characters : characterCounts)
	{
		if (BenchmarkCorpus::text(corpusLanguage).size() < characters)
			continue;

		const std::vector<Key> keys = streamOf<Key>(characters);

		// Filled once outside the region, which leaves the table where the first codec leaves it. detect() also
		// reserves a thousand trigrams up front, and every size here grows past that, so the state is the same.
		Map map;
		fill(map, keys);

		BENCHMARK_ADVANCED(caseName(container, key, characters))(Catch::Benchmark::Chronometer meter)
		{
			meter.measure([&map, &keys] {
				map.clear();
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
		// the set is the same for every container, and building one to reduce it would cost more than the case does.
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
	benchmarkBuild<AnkerlMap<CharsTrigram>>("ankerl::unordered_dense::map", "3 QChars");
	benchmarkBuild<AnkerlSegmentedMap<CharsTrigram>>("ankerl::unordered_dense::segmented_map", "3 QChars");
	benchmarkBuild<BoostMap<uint64_t>>("boost::unordered_flat_map", "uint64");
	benchmarkBuild<AnkerlMap<uint64_t>>("ankerl::unordered_dense::map", "uint64");
	benchmarkBuild<AnkerlSegmentedMap<uint64_t>>("ankerl::unordered_dense::segmented_map", "uint64");
	benchmarkBuild<StdMap<CharsTrigram>>("std::unordered_map", "3 QChars");
	benchmarkBuild<StdMap<uint64_t>>("std::unordered_map", "uint64");
}

TEST_CASE("Trigram table: refill after clear", "[!benchmark]")
{
	benchmarkRefill<BoostMap<CharsTrigram>>("boost::unordered_flat_map", "3 QChars");
	benchmarkRefill<AnkerlMap<CharsTrigram>>("ankerl::unordered_dense::map", "3 QChars");
	benchmarkRefill<AnkerlSegmentedMap<CharsTrigram>>("ankerl::unordered_dense::segmented_map", "3 QChars");
	benchmarkRefill<BoostMap<uint64_t>>("boost::unordered_flat_map", "uint64");
	benchmarkRefill<AnkerlMap<uint64_t>>("ankerl::unordered_dense::map", "uint64");
	benchmarkRefill<AnkerlSegmentedMap<uint64_t>>("ankerl::unordered_dense::segmented_map", "uint64");
	benchmarkRefill<StdMap<CharsTrigram>>("std::unordered_map", "3 QChars");
	benchmarkRefill<StdMap<uint64_t>>("std::unordered_map", "uint64");
}

TEST_CASE("Trigram table: build by sorting", "[!benchmark]")
{
	// What a flat container is actually good at: append every key, sort once, then count the runs.
	// The result is a sorted key array, the form a binary-searching lookup pass wants.
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
	benchmarkLookup<AnkerlMap<CharsTrigram>>("ankerl::unordered_dense::map", "3 QChars");
	benchmarkLookup<AnkerlSegmentedMap<CharsTrigram>>("ankerl::unordered_dense::segmented_map", "3 QChars");
	benchmarkLookup<BoostMap<uint64_t>>("boost::unordered_flat_map", "uint64");
	benchmarkLookup<AnkerlMap<uint64_t>>("ankerl::unordered_dense::map", "uint64");
	benchmarkLookup<AnkerlSegmentedMap<uint64_t>>("ankerl::unordered_dense::segmented_map", "uint64");
	benchmarkLookup<StdMap<CharsTrigram>>("std::unordered_map", "3 QChars");
	benchmarkLookup<StdMap<uint64_t>>("std::unordered_map", "uint64");
}
