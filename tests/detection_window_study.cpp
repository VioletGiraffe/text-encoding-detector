#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "benchmark_corpus.h"

#include "ctextencodingdetector.h"

DISABLE_COMPILER_WARNINGS
#include <QTextCodec>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <random>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

// How little of a file detection can read and still decode the whole of it correctly. detect() costs ~110 ms
// per MB of non-UTF-8 input, all of it linear, so a bounded sample makes the cost constant - if the sample
// still answers for the file it was drawn from.
//
// Hidden behind a '.' tag; it produces numbers rather than asserting:
//   text-encoding-detector-tests "[study]"
//
// The verdict is always taken over the whole file: a sample holding only ASCII decodes identically under
// every 8-bit codec, so it is right about itself and says nothing about the text around it.
//
// Sampling schemes at an equal byte budget:
//   prefix   - the first N bytes, which is what the simplest bounded window does
//   placed   - N contiguous bytes, reported at the worst of several placements
//   spread   - N/k bytes at k points spread evenly over the file, blind to content
//   anchored - N/k bytes at k points spread over the non-ASCII bytes, which are what decide the encoding
//
// Content matters more than size here. Prose carries its encoding in every window; a file that is mostly
// ASCII carries it in a few places, and a blind window can miss them all.

namespace {

struct Scenario
{
	std::string name;
	QString text;
	std::vector<const char*> codecNames; // The encodings this text is written in for the run
};

constexpr qsizetype windowSizes[] = { 16384, 65536 };
constexpr int chunkCount = 8;
// Many small windows rather than few large ones: a window centred on a non-ASCII byte carries its ASCII
// surroundings in with it, and those surroundings are what drown the signal in a mostly-ASCII file.
constexpr qsizetype denseChunkBytes = 128;
// Bytes kept around each anchor, swept to find where the surroundings start costing more than they carry
constexpr qsizetype contextSizes[] = { 1, 4, 16, 64, 256, 1024 };
constexpr qsizetype contextSweepBudget = 65536;
// The length of the English, code and json hosts; a language whose test prefix cannot fill the largest share
// of it gets shorter scenarios
constexpr qsizetype scenarioCharacters = 200000;
constexpr qsizetype interleavedBlockCharacters = 400;
constexpr qsizetype lineCharacters = 60;
constexpr double foreignShares[] = { 0.20, 0.05, 0.01 };
constexpr int nameWidth = 30;

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

// Each language whole, and each mixed into three hosts at three shares in three shapes. The Western European
// languages carry their non-ASCII characters one at a time inside ASCII words, which is a harder shape for a
// sampler than Russian's runs of Cyrillic - and it is their own accent density doing it, not a substitution rate.
[[nodiscard]] std::vector<Scenario> scenarios()
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

// k chunks of budget/k bytes, spread from the start of the data to its end
[[nodiscard]] QByteArray spreadSample(const QByteArray& data, qsizetype budget, int chunks)
{
	const qsizetype chunkSize = budget / chunks;

	QByteArray sample;
	sample.reserve(budget);
	for (int i = 0; i < chunks; ++i)
	{
		const qsizetype start = (data.size() - chunkSize) * i / (chunks - 1);
		sample.append(data.constData() + start, chunkSize);
	}

	return sample;
}

// The same budget, placed where the non-ASCII bytes are: a byte under 0x80 decodes the same under every
// candidate, so a window holding nothing else cannot choose between them.
// Finding them costs one scan at memory speed, against detection's ~110 ms per MB.
[[nodiscard]] QByteArray anchoredSample(const QByteArray& data, qsizetype budget, qsizetype chunkSize)
{
	std::vector<qsizetype> anchors;
	for (qsizetype i = 0; i < data.size(); ++i)
	{
		if (static_cast<unsigned char>(data[i]) >= 0x80)
			anchors.push_back(i);
	}

	if (anchors.empty())
		return data.left(budget);

	// Never more chunks than anchors: past that the same byte comes back repeatedly, which is a sample of
	// nothing the file contains
	const qsizetype chunks = std::min<qsizetype>(budget / chunkSize, (qsizetype)anchors.size());

	QByteArray sample;
	sample.reserve(budget);
	for (qsizetype i = 0; i < chunks; ++i)
	{
		const qsizetype anchor = anchors[(qsizetype)anchors.size() * i / chunks];
		const qsizetype start = std::clamp<qsizetype>(anchor - chunkSize / 2, 0, data.size() - chunkSize);
		sample.append(data.constData() + start, chunkSize);
	}

	return sample;
}

// Only the bytes the candidates disagree about. parse() already skips non-letters and carries its trigram
// window across them, so dropping every ASCII byte leaves the text's own trigrams less the English ones -
// which is the strict filter, applied to the bytes instead of to the parser.
// budget < 0 keeps the whole file; otherwise the kept bytes are spread over it rather than taken from the front.
[[nodiscard]] QByteArray nonAsciiOnly(const QByteArray& data, qsizetype budget)
{
	QByteArray kept;
	kept.reserve(budget > 0 ? budget : data.size());

	for (qsizetype i = 0; i < data.size(); ++i)
	{
		if (static_cast<unsigned char>(data[i]) >= 0x80)
			kept.append(data[i]);
	}

	if (budget < 0 || kept.size() <= budget)
		return kept;

	// Every nth byte would splice unrelated letters together; whole runs keep the trigrams a text really has
	return anchoredSample(kept, budget, denseChunkBytes);
}

// Two readings that part over a handful of characters are one answer: KOI8-R and KOI8-U differ in eight code
// points, so a single box-drawing glyph separates them while the text stays the same. The tolerance follows
// the non-ASCII count, which is what a codec choice actually decides.
[[nodiscard]] bool sameText(const QString& candidate, const QString& correct)
{
	if (candidate.size() != correct.size())
		return false;

	qsizetype differing = 0;
	qsizetype nonAscii = 0;
	for (qsizetype i = 0; i < correct.size(); ++i)
	{
		if (correct[i].unicode() >= 0x80)
			++nonAscii;
		if (candidate[i] != correct[i])
			++differing;
	}

	return differing <= std::max<qsizetype>(2, nonAscii / 1000);
}

// The two failures differ in what the viewer shows: a wrong answer renders as mojibake, no answer at all
// sends decode() back empty and the caller to its fallback encoding.
struct Outcome
{
	int samples = 0;
	int wrong = 0;
	int noAnswer = 0;
	double worstScore = 0.0;
	// Winner against the closest reading of the same sample that decodes it differently. A guard can compute
	// this at run time, where it cannot know whether the winner is right.
	double worstMargin = 2.0;

	void add(const Outcome& other)
	{
		samples += other.samples;
		wrong += other.wrong;
		noAnswer += other.noAnswer;
		worstScore = std::max(worstScore, other.worstScore);
		worstMargin = std::min(worstMargin, other.worstMargin);
	}
};

using DetectionResults = std::vector<CTextEncodingDetector::EncodingDetectionResult>;
using Scorer = std::function<DetectionResults(const QByteArray&)>;

// The library's own detection; a scorer of the study's own can stand in to try a scoring the library does not have
const Scorer libraryDetect = [](const QByteArray& sample) { return CTextEncodingDetector::detect(sample); };

// The sample chooses the encoding; the whole file is what that choice has to decode correctly
[[nodiscard]] Outcome judge(const QByteArray& sample, const QByteArray& wholeFile, QTextCodec* correctCodec, const Scorer& score = libraryDetect)
{
	Outcome outcome;
	outcome.samples = 1;

	const DetectionResults results = score(sample);
	// decode() discards a best score of 0.95 or worse and returns nothing at all
	if (results.empty() || results.front().score >= 0.95)
	{
		outcome.noAnswer = 1;
		outcome.worstScore = results.empty() ? 1.0 : results.front().score;
		return outcome;
	}

	outcome.worstScore = results.front().score;

	QTextCodec* const winner = QTextCodec::codecForName(results.front().encoding.toLatin1());
	const bool correct = winner && sameText(winner->toUnicode(wholeFile), correctCodec->toUnicode(wholeFile));
	outcome.wrong = correct ? 0 : 1;

	// Sorted by score, so the first reading that decodes the sample differently is the closest rival
	const QString winningText = winner ? winner->toUnicode(sample) : QString{};
	for (const auto& result : results)
	{
		QTextCodec* const candidate = QTextCodec::codecForName(result.encoding.toLatin1());
		if (!candidate || sameText(candidate->toUnicode(sample), winningText))
			continue;

		outcome.worstMargin = result.score - results.front().score;
		break;
	}

	return outcome;
}

[[nodiscard]] std::string formatted(const Outcome& outcome)
{
	std::ostringstream cell;
	cell << std::fixed << std::setprecision(2) << std::setw(6) << outcome.worstScore;
	if (outcome.wrong > 0)
		cell << "  MOJIBAKE " << std::setw(2) << outcome.wrong << "/" << std::setw(2) << outcome.samples;
	else if (outcome.noAnswer > 0)
		cell << "  gave up  " << std::setw(2) << outcome.noAnswer << "/" << std::setw(2) << outcome.samples;
	else
		cell << "               ";

	return cell.str();
}

}

TEST_CASE("Detection accuracy against the size and placement of the sample it reads", "[.study]")
{
	const std::vector<Scenario> all = scenarios();
	REQUIRE(!all.empty());

	std::cout << "\nEach cell: worst winning score, and whether the winner decodes the WHOLE file correctly.\n"
		<< "Scenarios are up to " << scenarioCharacters / 1024 << "K characters; the share is how much of it is not the host.\n";

	// Every scenario encoded once per codec it claims to be written in, which is what each scheme then samples
	std::vector<std::vector<QTextCodec*>> scenarioCodecs;
	std::vector<std::vector<QByteArray>> encodedScenarios;

	for (const Scenario& scenario : all)
	{
		std::vector<QTextCodec*> codecs;
		std::vector<QByteArray> perCodec;

		for (const char* codecName : scenario.codecNames)
		{
			QTextCodec* const codec = QTextCodec::codecForName(codecName);
			REQUIRE(codec);
			codecs.push_back(codec);

			const std::unique_ptr<QTextEncoder> encoder{ codec->makeEncoder(QTextCodec::IgnoreHeader) };
			perCodec.push_back(encoder->fromUnicode(scenario.text));
		}

		scenarioCodecs.push_back(std::move(codecs));
		encodedScenarios.push_back(std::move(perCodec));
	}

	const auto overScenario = [&](size_t index, auto&& makeSample) {
		Outcome outcome;
		for (size_t c = 0; c < scenarioCodecs[index].size(); ++c)
		{
			const QByteArray& data = encodedScenarios[index][c];
			outcome.add(judge(makeSample(data), data, scenarioCodecs[index][c]));
		}

		return outcome;
	};

	std::cout << "\n=== standalone: the whole file, before and after dropping the ASCII ===\n"
		<< std::setw(nameWidth) << "content" << "   whole file (today)     non-ASCII only        kept\n";

	for (size_t i = 0; i < all.size(); ++i)
	{
		const Outcome whole = overScenario(i, [](const QByteArray& data) { return data; });
		const Outcome filtered = overScenario(i, [](const QByteArray& data) { return nonAsciiOnly(data, -1); });
		const qsizetype keptShare = 100 * nonAsciiOnly(encodedScenarios[i][0], -1).size() / encodedScenarios[i][0].size();

		std::cout << std::setw(nameWidth) << all[i].name << "   " << formatted(whole) << "  " << formatted(filtered)
			<< "  " << std::setw(3) << keptShare << "%\n";
	}

	for (const qsizetype budget : windowSizes)
	{
		std::cout << "\n=== composed: sample budget " << budget / 1024 << " KB ===\n"
			<< std::setw(nameWidth) << "content" << "   prefix (blind)         spread " << chunkCount << " (blind)      anchored "
			<< denseChunkBytes << "B            non-ASCII only\n";

		for (size_t i = 0; i < all.size(); ++i)
		{
			const Outcome prefix = overScenario(i, [budget](const QByteArray& data) { return data.left(budget); });
			const Outcome spread = overScenario(i, [budget](const QByteArray& data) { return spreadSample(data, budget, chunkCount); });
			const Outcome anchored = overScenario(i, [budget](const QByteArray& data) { return anchoredSample(data, budget, denseChunkBytes); });
			const Outcome filtered = overScenario(i, [budget](const QByteArray& data) { return nonAsciiOnly(data, budget); });

			std::cout << std::setw(nameWidth) << all[i].name
				<< "   " << formatted(prefix)
				<< "  " << formatted(spread)
				<< "  " << formatted(anchored)
				<< "  " << formatted(filtered) << "\n";
		}
	}

	std::cout << "\n=== context kept around each anchor, at a " << contextSweepBudget / 1024 << " KB budget ===\n"
		<< std::setw(nameWidth) << "content";
	for (const qsizetype context : contextSizes)
		std::cout << std::setw(11) << context << "B    ";

	std::cout << "\n";

	for (size_t i = 0; i < all.size(); ++i)
	{
		std::cout << std::setw(nameWidth) << all[i].name;
		for (const qsizetype context : contextSizes)
		{
			const Outcome outcome = overScenario(i, [context](const QByteArray& data) {
				return anchoredSample(data, contextSweepBudget, context);
			});

			std::cout << "  " << formatted(outcome);
		}

		std::cout << "\n";
	}

	std::cout << "\n=== confidence: the winner against the closest reading that decodes differently ===\n"
		<< "A run-time guard can see this; it cannot see whether the winner is right.\n"
		<< std::setw(nameWidth) << "content" << "   whole file (today)      anchored 128B, 64 KB\n";

	for (size_t i = 0; i < all.size(); ++i)
	{
		const Outcome whole = overScenario(i, [](const QByteArray& data) { return data; });
		const Outcome anchored = overScenario(i, [](const QByteArray& data) { return anchoredSample(data, 65536, denseChunkBytes); });

		const auto scoreAndMargin = [](const Outcome& outcome) {
			std::ostringstream cell;
			cell << std::fixed << std::setprecision(2) << std::setw(6) << outcome.worstScore
				<< " margin" << std::setw(6) << outcome.worstMargin
				<< (outcome.wrong > 0 ? "  MOJIBAKE" : outcome.noAnswer > 0 ? "  gave up " : "          ");
			return cell.str();
		};

		std::cout << std::setw(nameWidth) << all[i].name << "   " << scoreAndMargin(whole) << "  " << scoreAndMargin(anchored) << "\n";
	}

	std::cout << std::endl;
}
