#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "benchmark_corpus.h"
#include "mixed_content_scenarios.h"

#include "ctextencodingdetector.h"

DISABLE_COMPILER_WARNINGS
#include <QTextCodec>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// How little of a file detection can read and still decode the whole of it correctly. detect() is linear in its
// input with a large constant (PROCEDURE.md has the figures), so decode() hands it a bounded sample; this measures
// the schemes and budgets that sample could use, against the whole file.
//
// Hidden behind a '.' tag; it produces numbers rather than asserting:
//   text-encoding-detector-tests "[study]"
//
// The verdict is always taken over the whole file: a sample holding only ASCII decodes identically under
// every 8-bit codec, so it is right about itself and says nothing about the text around it.
//
// Sampling schemes at an equal byte budget:
//   prefix   - the first N bytes, which is what the simplest bounded window does
//   spread   - N/k bytes at k points spread evenly over the file, blind to content
//   anchored - N/k bytes at k points spread over the non-ASCII bytes, which are what decide the encoding
//
// Content matters more than size here. Prose carries its encoding in every window; a file that is mostly
// ASCII carries it in a few places, and a blind window can miss them all.

namespace {

constexpr qsizetype windowSizes[] = { 16384, 65536 };
constexpr int chunkCount = 8;
constexpr qsizetype denseChunkBytes = CTextEncodingDetector::detectionSampleChunk;
constexpr int nameWidth = 30;

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
	const std::vector<MixedContent::Scenario> all = MixedContent::scenarios();
	REQUIRE(!all.empty());

	std::cout << "\nEach cell: worst winning score, and whether the winner decodes the WHOLE file correctly.\n"
		<< "Scenarios are up to " << MixedContent::scenarioCharacters / 1024 << "K characters; the share is how much of it is not the host.\n";

	// Every scenario encoded once per codec it claims to be written in, which is what each scheme then samples
	std::vector<std::vector<QTextCodec*>> scenarioCodecs;
	std::vector<std::vector<QByteArray>> encodedScenarios;

	for (const MixedContent::Scenario& scenario : all)
	{
		std::vector<QTextCodec*> codecs;
		std::vector<QByteArray> perCodec;

		for (const char* codecName : scenario.codecNames)
		{
			QTextCodec* const codec = QTextCodec::codecForName(codecName);
			REQUIRE(codec);
			codecs.push_back(codec);
			perCodec.push_back(BenchmarkCorpus::encoded(scenario.text, codecName));
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

	for (const qsizetype budget : windowSizes)
	{
		std::cout << "\n=== composed: sample budget " << budget / 1024 << " KB ===\n"
			<< std::setw(nameWidth) << "content" << "   prefix (blind)         spread " << chunkCount << " (blind)      anchored "
			<< denseChunkBytes << "B\n";

		for (size_t i = 0; i < all.size(); ++i)
		{
			const Outcome prefix = overScenario(i, [budget](const QByteArray& data) { return data.left(budget); });
			const Outcome spread = overScenario(i, [budget](const QByteArray& data) { return spreadSample(data, budget, chunkCount); });
			const Outcome anchored = overScenario(i, [budget](const QByteArray& data) { return CTextEncodingDetector::anchoredSample(data, budget, denseChunkBytes); });

			std::cout << std::setw(nameWidth) << all[i].name
				<< "   " << formatted(prefix)
				<< "  " << formatted(spread)
				<< "  " << formatted(anchored) << "\n";
		}
	}

	std::cout << "\n=== whole file and the 64 KB anchored sample: the winner against the closest reading that decodes differently ===\n"
		<< "A run-time guard can see this; it cannot see whether the winner is right.\n"
		<< std::setw(nameWidth) << "content" << "   whole file              anchored 128B, 64 KB\n";

	for (size_t i = 0; i < all.size(); ++i)
	{
		const Outcome whole = overScenario(i, [](const QByteArray& data) { return data; });
		const Outcome anchored = overScenario(i, [](const QByteArray& data) { return CTextEncodingDetector::anchoredSample(data, 65536, denseChunkBytes); });

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
