# Testing the detector: how it is set up, and what it has shown

Covers the corpus pipeline, the test project, and the findings the two produced. Numbers are from an
i5-12600K, Release, Qt 6.11.1.

## The corpus

`corpus/` holds real prose in six languages, committed. `corpus/README.md` lists sources and per-file counts;
`prepare_corpus.ps1` rebuilds them and reproduces the committed bytes exactly.

Each work is split in two. `corpus/test/` is a prefix the tests, benchmarks and study read; `corpus/train/` is
the remainder, which `text-analyzer` builds the trigram tables from. Nothing the tests score has been seen by
a table — scoring text against tables built from that same text would flatter every number here.

The tables are regenerated from the tables directory, one language at a time:

```
cd text-encoding-detector/src/trigramfrequencytables
text_analyzer French ../../../corpus/train/french.txt
```

Committed rather than generated, for two reasons found the hard way:

- Text synthesized from the trigram tables and then scored against those same tables flatters the detector.
  Fine for throughput, useless for accuracy.
- Text gathered from whatever is on disk is not reproducible. An earlier version of the study walked the
  repository for `*.cpp` and got `moc_*.cpp` and `qrc_*.cpp` resource blobs as its "English prose", which
  changed the numbers materially. See *What the corpus changed* below.

**The contract each file meets: it survives its target 8-bit codecs without losing a character.** A character
a codec cannot carry encodes as `?`, an ASCII byte, which both distorts the trigram statistics and acts as
ASCII filler inside text that is meant to be non-ASCII. `prepare_corpus.ps1` fails rather than writing a file
that breaks this, and a test re-checks it at run time.

The Cyrillic codecs are the strict ones: KOI8-R and CP866 carry no en dash, no guillemets and no accented
Latin, all of which ordinary Russian prose contains. English is reduced to pure ASCII deliberately — it is the
codec-independent half of every mixed-content scenario, and one non-ASCII character in it would be a false
anchor for a sampler that seeks them out.

Gutenberg supplies five languages. It cannot supply Russian: of its nine Russian entries the three prose works
are audiobooks with no text, leaving an arithmetic textbook and 18th-century odes. Russian comes from a local
copy of *Anna Karenina*, passed in with `-RussianSource`.

## The test project

`tests/` is a Catch2 project built the way `image-processing/tests` is. Add `CONFIG+=build_tests` to the qmake
run, or open `tests/text-encoding-detector-tests.pro` directly.

| file | what it holds |
| --- | --- |
| `benchmark_corpus.{h,cpp}` | the corpus: decoding, slicing, encoding into a named codec |
| `ctextencodingdetector_tests.cpp` | correctness, including the corpus contract |
| `ctextencodingdetector_benchmarks.cpp` | what `decode()` costs and where the cost goes |
| `trigram_container_benchmarks.cpp` | the trigram table's container and key shape |
| `detection_window_study.cpp` | how little of a file detection can read and still be right |

Three ways to run it:

```
text-encoding-detector-tests                                              # tests only
text-encoding-detector-tests "[!benchmark]" -r fastest --benchmark-no-analysis
text-encoding-detector-tests "[study]"
```

CI (`.github/workflows/CI.yml`) builds the tests and `text-analyzer` on Windows, Linux and macOS, runs the
tests, and runs the benchmarks with a handful of samples as a smoke test only: a shared runner's timings are
not comparable to the numbers below.

Benchmarks and the study are hidden behind tags, so a plain run stays fast. The benchmark reporter is
`cpp-template-utils/tests/catch_benchmark_reporter.hpp`, which reports the mean of the fastest third of the
samples — interference only ever adds time, so the fastest samples are the honest ones.

### How the study works

It builds scenarios out of the corpus — each language whole, and each mixed into English at 20%, 5% and 1%,
clustered in one run or interleaved in 400-character blocks — encodes each into the codecs its language is
written in, and hands `detect()` a *sample* of the result.

Two rules make its verdicts mean something:

- **The verdict is taken over the whole file, never the sample.** A sample holding only ASCII decodes
  identically under every 8-bit codec, so it is right about itself and says nothing about the text around it.
- **Two failures are distinguished.** "MOJIBAKE" is a wrong answer under the 0.95 plausibility threshold, which
  the caller cannot detect. "gave up" is a score at or above it, where `decode()` returns nothing and the
  caller falls back. The second is a far better outcome than the first.

Readings that differ in a handful of characters count as one answer — KOI8-R and KOI8-U part over eight code
points, and a single box-drawing glyph would otherwise register as a different verdict.

## Findings: performance

**`decode()` takes one of three routes, and only the bytes of the input decide which.** A NUL byte anywhere
declines the input as binary before any probe runs. Valid UTF-8, pure ASCII included, is answered by
`isUtf8()` and never reaches `detect()`. Everything else — every 8-bit encoding, and any binary file without a
NUL byte — runs the full detection.

| input | 64 KB | 256 KB | 1.3 MB |
| --- | --- | --- | --- |
| valid UTF-8 | 580 µs | 2.40 ms | 7.65 ms |
| Windows-1251, six language tables | 9.71 ms | 35.0 ms | — |
| binary on the slow route (the test executable, measured before the guard) | 17.0 ms | 58.3 ms | — |

**Roughly 5.6 ms per MB on the fast route against ~140 ms per MB on the slow one.** Binary is worse than text,
because garbage produces more distinct trigrams than language does; the guard stops anything with a NUL byte
at that byte, and the row above is what a binary file without one still costs.

Going from two language tables to six added ~3 ms at 256 KB: each table is one more scoring pass per codec,
and the model's own norm is precomputed at construction so a pass costs only the sample's lookups.

Where the slow route's time goes, per codec, at 256 KB — and `detect()` tries about a dozen:

| phase | cost | share |
| --- | --- | --- |
| `parse()` | 3.78 ms | 88% |
| codec decode | 460 µs | 11% |
| dedup hash | 44 µs | 1% |
| all six frequency tables, built once per process | 1.63 ms | first call only |

At 4 KB of input the table build is two thirds of the first call. Inside `parse()`, the hash map is about a
quarter and the character scan — `isLetter()` plus `toLower()`, two Unicode table lookups per character — is
the rest. The scan is the single largest line item in the detector.

### The trigram container

Both workloads, at 768 K characters. Build is insert-or-increment, what `parse()` does; lookup is `find()`
against the baked model, what `cosineDistance()` does.

| container | build, Trigram key | build, uint64 key | lookup, Trigram | lookup, uint64 |
| --- | --- | --- | --- | --- |
| `boost::unordered_flat_map` | **3.21 ms** | **1.91 ms** | **65.9 µs** | **33.4 µs** |
| `ankerl::unordered_dense` | 9.27 ms | 7.36 ms | 134 µs | 88.2 µs |
| `flat_map` (sorted vector) | 5.35 ms @ 64 K, where boost takes 352 µs | — | 399 µs | — |
| sort and count runs | 26.1 ms | — | — | — |

**Boost wins every cell by 2–4×**, which is unusual enough to be worth recording: `unordered_dense` normally
matches or beats it. The sorted vector is not competitive on either workload, and neither is the flat-container
approach of appending everything and sorting once. Packing the three `QChar` into a `uint64` is worth 1.7× on
build and 2.0× on lookup.

## Findings: correctness

Listed roughly in the order they were found, because several overturned earlier conclusions.

**1. `CTextParser::parse()` counted the wrong characters.** Its seed loop tested an inverted predicate and
accumulated the characters it had just decided to skip, so the first trigram came from the first three
punctuation marks and every letter before them was dropped. Worse, a text with fewer than three punctuation
characters made `parse()` return false, and `detect()` then skipped that codec entirely. Fixed: one loop, one
predicate, and the sliding window itself signals when it is full.

**2. `isUtf8()` accepts UTF-16 and binary.** It is `fromUtf8(data).toUtf8() == data`, and UTF-16 text in any
alphabet below U+0800 is entirely bytes under 0x80 — NUL bytes included. Measured on the corpus: **244,338
characters of UTF-16LE Russian were accepted as UTF-8** before one U+00A0 broke the spell. The same rule makes
it the guard that decides "this is text" for binary files, and it says yes to a stream that is 19% NUL.
`decode()` now declines any input with a NUL byte before probing, which covers both — at the price of BOM-less
UTF-16/32 being declined instead of detected. `isUtf8()` itself is unchanged: a NUL is valid UTF-8.

**3. Whole-file detection is confident mojibake on mixed content — including Russian.** With real English
prose as the ASCII half, `detect()` gets *every* mixed scenario wrong at scores of 0.09–0.12, which is maximum
confidence. Only pure Russian prose is answered correctly.

**4. The mechanism: the scoring rewards a codec for destroying what it cannot match.** For Western European
text the winner is *UTF-8*, which turns every accented byte into U+FFFD. Replacement characters are not
letters, so `parse()` drops them and closes the surrounding letters over the gap, yielding trigrams the English
model knows. The correct reading instead produces accented trigrams the model has never seen, and scores worse.
ASCII decodes identically under every candidate, so it contributes equal mass to all of them and decides
nothing — it only dilutes.

**5. Blind sampling makes this worse, not better.** A bounded window placed without regard to content —
prefix, or chunks spread evenly — is mojibake on every mixed scenario, at every budget from 16 KB to the whole
file. It converts a fallback into a silent wrong answer.

**6. Dropping the ASCII fixes it.** Feeding `detect()` only the non-ASCII bytes, in their original runs, gets
every Russian scenario right — 0.12 for prose, 0.44 at 1% Russian — and makes every Western European scenario
*decline* rather than lie. It is also nearly free on mixed content: the filtered stream is 15% of a 20% file
and under 1% of a 1% file. On pure Russian prose it is 77%, so it saves nothing there and a budget cap is what
bounds the cost.

**7. Context around each non-ASCII byte is a trap.** Keeping 128 bytes around each anchor is enough ASCII for
the English model to win again, and Western European goes back to mojibake. Keeping 1–4 bytes splices letters
that were never adjacent and manufactures trigrams the file never had. Keeping the non-ASCII bytes *in their
own runs* avoids both.

**8. The margin discriminates perfectly where the score does not.** Comparing the winner against the closest
reading that decodes differently:

| | correct answers | wrong answers |
| --- | --- | --- |
| absolute score | 0.12 – 0.44 | 0.09 – 0.85 — overlapping, so useless |
| **margin** | **0.43 – 0.72** | **0.00 – 0.01** |

A run-time guard can compute this; it cannot know whether the winner is right. A gate around 0.2 rejects every
mojibake case in the study and keeps every strong correct answer.

**9. Western European was not detectable with English and Russian tables alone.** Accented Latin matches
neither the English model, whose trigrams hold no accents, nor the Russian one. Fixed by tables for French,
German, Spanish and Polish built from `corpus/train/`: each of the four is now read back in the right encoding
*and* the right language, on whole-file detection, with no other change. The `[!shouldfail]` test that pinned
the defect is now a plain passing test.

### The design these point to

1. Sample by dropping every ASCII byte, keeping the rest in their original runs, capped at a budget.
2. Accept the winner only if its margin over the closest differently-decoding candidate is clear.
3. Otherwise return nothing and let the caller fall back.

Cost is then constant in file size — about 10 ms — instead of ~127 ms per MB, and correctness improves at the
same time. Two of the performance items above then stop mattering: with a bounded, filtered sample the
container choice and the packed key are worth single-digit milliseconds on work that no longer dominates.

## What the corpus changed

Worth recording, because it invalidated conclusions that looked solid.

Before the corpus existed, the study's "English" was the repository's own build output — `moc_*.cpp` and
`qrc_*.cpp` hex blobs — and its "Western European" was English with accents substituted in at a set rate. On
that data, whole-file detection appeared to *give up safely* on Western European, and a 64 KB sample in eight
chunks with 64 bytes of context around each anchor looked like the recommended design.

Both were wrong. Real English prose matches the English model far better than generated C++ does, which is
what lets the mangling-codec win outright; and real French, German, Spanish and Polish carry their accents in
distributions no substitution rate reproduces. The corrected answer — filter, do not merely sample, and gate on
margin — only appeared once the corpus was real.

The lesson generalizes: **for an accuracy study, synthetic inputs are worth exactly as much as their realism,
and a synthetic input that is wrong in a way you have not thought of returns a confident wrong answer.**

## Known open items

- `decode()` decodes the winning encoding a second time, having discarded the copy `detect()` made.
- The NUL guard is broad both ways: a NUL-terminated text file is declined, and a binary file without a NUL
  byte still runs the full detection.
- The text viewer's explicit "as UTF-8" action does not use the binary guard.
- The regenerated tables are thinner than the originals: English 3,520 entries against 17,549, Russian 6,006
  against 14,662, the originals coming from training sets several times larger. Whether that costs accuracy
  is for the study to say, by pulling the original tables from git history and scoring both sets — once the
  study covers mixed files well enough for the comparison to mean something.
