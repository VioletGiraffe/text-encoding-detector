# Testing the detector: how it is set up, and what it has shown

Covers the corpus pipeline, the test project, and the findings the two produced. Numbers are from an
i5-12600K, Release, Qt 6.11.1.

## The corpus

`corpus/` holds real prose in six languages, committed. `corpus/README.md` lists sources and per-file counts;
`prepare_corpus.ps1` rebuilds them and reproduces the committed bytes exactly.

Each work is split in two. `corpus/test/` is a prefix the tests, benchmarks and study read; `corpus/train/` is
the remainder, which `text-analyzer` builds the trigram tables from. Nothing the tests score has been seen by
a table — scoring text against tables built from that same text would flatter every number here.

There are five tables, not six: an English one would hold only all-ASCII trigrams, which scoring ignores (see
finding 13), and pure ASCII never reaches `detect()`. English's test prefix is the ASCII host of the mixed-content
scenarios.

The tables are regenerated from the tables directory, one language at a time; the optional last argument is
the minimum occurrence count a trigram needs to be kept (default 10):

```
cd text-encoding-detector/src/trigramfrequencytables
text_analyzer French ../../../corpus/train/french.txt 10
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

Russian also has a second author, test-only: Kuprin's *Поединок*, fetched from Wikisource at pinned page
revisions. The Tolstoy test prefix shows what the Russian table was fit to; Kuprin shows what it generalizes to
(finding 16 measured that on texts that cannot be committed).

## The test project

`tests/` is a Catch2 project built the way `image-processing/tests` is. Add `CONFIG+=build_tests` to the qmake
run, or open `tests/text-encoding-detector-tests.pro` directly.

| file | what it holds |
| --- | --- |
| `benchmark_corpus.{h,cpp}` | the corpus: decoding, slicing, encoding into a named codec |
| `mixed_content_scenarios.{h,cpp}` | each language whole and mixed into three hosts at three shares in three shapes |
| `ctextencodingdetector_tests.cpp` | correctness: the corpus contract and the shortlist resolving; `decode()` over every language, the second author, every mixed-content scenario whole and grown past the sample budget, in every codec, with the winning score capped at 0.90; the wide encodings with and without a mark; binary declined |
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

Benchmarks and the study are hidden behind tags; a plain run is mostly the mixed-content matrix, about nine
seconds on the machine above. The benchmark reporter is
`cpp-template-utils/tests/catch_benchmark_reporter.hpp`, which reports the mean of the fastest third of the
samples — interference only ever adds time, so the fastest samples are the honest ones.

### How the study works

Its scenarios are those of `mixed_content_scenarios.cpp`, which the tests assert over whole — each language
whole, and each mixed into three ASCII hosts (English prose, C source, a JSON log) at 20%, 5% and 1%, in three
shapes: clustered in one run, interleaved in 400-character blocks, or single 60-character lines among the
host's lines, the shape of a comment or a log message. 140 scenarios; each is encoded into the codecs its
language is written in, and `detect()` is handed a *sample* of the result. Scenarios are 200 K characters,
shorter where the language's test prefix cannot fill the 20% share.

Two rules make its verdicts mean something:

- **The verdict is taken over the whole file, never the sample.** A sample holding only ASCII decodes
  identically under every 8-bit codec, so it is right about itself and says nothing about the text around it.
- **Two failures are distinguished.** "MOJIBAKE" is a wrong answer under the 0.95 plausibility threshold, which
  the caller cannot detect. "gave up" is a score at or above it, where `decode()` returns nothing and the
  caller falls back. The second is a far better outcome than the first.

Readings that differ in a handful of characters count as one answer — KOI8-R and KOI8-U part over eight code
points, and a single box-drawing glyph would otherwise register as a different verdict.

## Findings: performance

**`decode()` takes one of four routes, and only the bytes of the input decide which.** A byte order mark
names the encoding outright. A NUL byte anywhere is either BOM-less UTF-16/32, told by the phase its NULs keep
(finding 17), or binary, declined before any probe runs. Valid UTF-8, pure ASCII included, is answered by
`isUtf8()` and never reaches `detect()`. Everything else — every 8-bit encoding, and any binary file without a
NUL byte — runs the detection, on the whole input up to 256 KB and on a sample of that size past it (finding 18).

| input | 64 KB | 256 KB | 1.3 MB |
| --- | --- | --- | --- |
| valid UTF-8 | 585 µs | 2.33 ms | 7.51 ms |
| Windows-1251, eleven codecs, five language tables | 5.55 ms | 19.8 ms | 28.9 ms at 768 KB |

BOM-less UTF-16LE, the wide route: 770 µs at 128 KB, 3.21 ms at 512 KB, 9.91 ms at 1.5 MB — about 6.6 ms per
MB, the fast route's class.

**Roughly 5.6 ms per MB on the fast route against about 79 ms per MB on the slow one, which the sample caps.**
Past 256 KB the slow route costs the 20 ms of a 256 KB detection, two passes over the bytes and one decode of the
whole input: 28.9 ms at 768 KB. Binary is worse than text at the same size, because garbage produces more distinct
trigrams than language does; the guard stops anything with a NUL byte at that byte, and a binary file without one
still runs the whole detection.

Each language table is one more scoring pass per codec; the model's own norm is precomputed at construction,
and only the sample's non-ASCII trigrams are looked up, so a pass is cheap next to `parse()`.

Where the slow route's time goes, per codec, at 256 KB — and `detect()` tries eleven, plus the locale's:

| phase | cost | share |
| --- | --- | --- |
| `parse()` | 1.26 ms | 72% |
| codec decode | 443 µs | 25% |
| dedup hash | 42 µs | 2% |
| all five frequency tables, built once per process | 0.56 ms | first call only |

At 4 KB of input the table build is two thirds of the first call. Inside `parse()` the trigram map costs more than
the character scan, and both are shaped for it.

The scan reads a flat table of the lowercase letter for every code point below U+0500, null for everything that is
not a letter, which answers `isLetter()` and `toLower()` in one lookup; `QChar` answers above it. The table is
built from `QChar` on first use, so no Unicode data is written out here. Its width covers Latin with its
supplements and extensions, Greek and Cyrillic — every script the frequency tables have a language for. A table of
only the 128 ASCII code points instead costs Cyrillic prose 6%: its branch flips at every word boundary.

The map's key is the three code points packed into the low 48 bits of a `uint64`, hashed by splitmix64's
finalizer. It is worth more than the scan table: `parse()` runs 2.1 to 2.5 times faster than with a key of three
`QChar` hashed as six bytes, on every script measured.

Per 128 K characters, the whole `decode()` call on the slow route:

| input | `decode()` |
| --- | --- |
| 1% Cyrillic clustered in a JSON host | 2.6 ms |
| 5% Cyrillic interleaved into C source | 6.1 ms |
| French prose, ISO-8859-1 | 7.4 ms |
| Russian prose, Windows-1251 | 11.0 ms |

A file mixed into an ASCII host is the most ASCII input that reaches detection at all — one with no non-ASCII byte
is valid UTF-8, and `isUtf8()` answers it.

### The trigram container

Both workloads, at 768 K characters. Build is insert-or-increment, what `parse()` does; lookup is `find()`
against the baked model, what `cosineDistance()` does.

| container | build, 3 QChars | build, uint64 | lookup, 3 QChars | lookup, uint64 |
| --- | --- | --- | --- | --- |
| `boost::unordered_flat_map` | **3.21 ms** | **1.91 ms** | **65.9 µs** | **33.4 µs** |
| `ankerl::unordered_dense` | 9.27 ms | 7.36 ms | 134 µs | 88.2 µs |
| `flat_map` (sorted vector) | 5.35 ms @ 64 K, where boost takes 352 µs | — | 399 µs | — |
| sort and count runs | 26.1 ms | — | — | — |

**Boost wins every cell by 2–4×**, which is unusual enough to be worth recording: `unordered_dense` normally
matches or beats it. The sorted vector is not competitive on either workload, and neither is the flat-container
approach of appending everything and sorting once. Packing the three `QChar` into a `uint64` is worth 1.7× on
build and 2.0× on lookup here, and more than that inside `parse()`, where the key is produced in registers rather
than read from a vector prepared outside the measured region.

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
`decode()` now declines any input with a NUL byte before probing, which covers both; finding 17 carves the
wide encodings back out of that rule. `isUtf8()` itself is unchanged: a NUL is valid UTF-8.

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

**6. Dropping the ASCII fixes it — for Russian; see finding 10.** Feeding `detect()` only the non-ASCII bytes, in their original runs, gets
every Russian scenario right — 0.12 for prose, 0.44 at 1% Russian — and makes every Western European scenario
*decline* rather than lie. It is also nearly free on mixed content: the filtered stream is 15% of a 20% file
and under 1% of a 1% file. On pure Russian prose it is 77%, so it saves nothing there and a budget cap is what
bounds the cost.

**7. Context around each non-ASCII byte is a trap.** Keeping 128 bytes around each anchor is enough ASCII for
the English model to win again, and Western European goes back to mojibake. Keeping 1–4 bytes splices letters
that were never adjacent and manufactures trigrams the file never had. Keeping the non-ASCII bytes *in their
own runs* avoids both.

**8. The margin discriminates perfectly where the score does not — for Russian; see finding 12.** Comparing
the winner against the closest reading that decodes differently:

| | correct answers | wrong answers |
| --- | --- | --- |
| absolute score | 0.12 – 0.44 | 0.09 – 0.85 — overlapping, so useless |
| **margin** | **0.43 – 0.72** | **0.00 – 0.01** |

A run-time guard can compute this; it cannot know whether the winner is right. A gate around 0.2 rejects every
mojibake case in the study and keeps every strong correct answer.

**9. Western European was not detectable with English and Russian tables alone.** Accented Latin matches
neither the English model, whose trigrams hold no accents, nor the Russian one. Fixed by tables for French,
German, Spanish and Polish built from `corpus/train/`: each of the four is now read back in the right encoding
*and* the right language, on whole-file detection, with no other change. The test that pinned the defect is
now a plain passing one.

**10. Dropping the ASCII bytes destroys Western European detection.** With a table for each language,
whole-file detection reads French, German, Spanish and Polish prose correctly — and the non-ASCII-only sample
of the same prose is declined at 0.97–1.00. Those languages carry their non-ASCII characters one at a time
inside ASCII words; the filtered stream is accented letters back to back, trigrams no table has ever seen. The
filter worked for Russian because Cyrillic comes in whole words. Finding 6 was a Russian result, not a design.

**11. Whole-file detection is mojibake on every mixed scenario in every host, with one exception.** All five
languages, three hosts, three shapes, three shares: wrong, at scores from 0.01 (English host, maximum
confidence) through 0.40 (C source) to 0.85 (JSON). The exception is the 20% share in the JSON host, which is
right for every language: JSON carries almost no letters, so at a fifth prose the prose owns the trigrams. At
5% it does not. The mixing shape makes no difference at all.

**12. The margin is not a discriminator for Western European text.** Correct whole-file readings of French,
German and Spanish prose win by 0.00–0.01; Polish by 0.06. Some 98% of a Western European text's trigrams are
pure ASCII and decode identically under every candidate, including the UTF-8 reading that turns every accent
into a replacement character — so the whole-text cosine of the right reading and of a mangled one differ by
about the share of trigrams that carry an accent. Russian margins remain 0.4–0.8. A gate at 0.2 would reject
every correct Western European answer. Finding 8 was also a Russian result.

**13. Scoring only the trigrams that carry a non-ASCII character fixes all of it.** The filter of finding 6,
applied at the trigram instead of the byte: `cosineDistance()` skips every all-ASCII sample trigram, and the
table's norm is precomputed over its non-ASCII trigrams alone. A trigram spanning an accent keeps its ASCII
neighbours, so it is one the table has seen; a reading that mangles the accent into a replacement character
keeps no trigram at all and scores 1.0; one that maps it to the wrong letter keeps trigrams no table has. The
host contributes nothing under any codec, and the English table never wins, which is right — pure ASCII never
reaches `detect()`.

With it, **whole-file detection is correct on all 140 scenarios**. The score now tracks the share of prose,
not the host or the shape: French 0.06 pure, 0.17 at 20%, 0.33 at 5%, 0.66 at 1%, and the same three numbers
whether the host is English, C or JSON. Polish at 1% is the highest at 0.81, still under the 0.95 threshold.
Margins are 0.07–0.48 for the Western European languages, 0.66–0.81 for Polish; no gate is needed, since
nothing is wrong, and none would be safe at German's 0.07.

The 64 KB anchored sample — 128-byte chunks centred on non-ASCII bytes — is also correct on all 140, at a
constant cost. Blind prefix and spread samples are never wrong any more either: a sample with no non-ASCII
byte has nothing to score and is declined, which is the honest answer for a sample that cannot know.

**14. The tables hold only what scoring reads, and the occurrence cut does not matter.** `text-analyzer` now
leaves out all-ASCII trigrams, which under finding 13 were inert rows: the Western European tables were 80–90%
of them, and the English table entirely — it is gone, since English is not an encoding question and pure ASCII
never reaches `detect()`. The minimum occurrence count a trigram needs to be kept was swept at 10, 5, 2 and 1:
every score and margin in the study moved by 0.01 or less, while the tables grew three- to four-fold. Counts
weight the cosine, and a trigram seen twice weighs nothing beside one seen a thousand times. The cut stays at 10.

**15. The original tables against the regenerated ones, with and without the filter.** The original English
and Russian pair (commit `fec47f7`) and the six regenerated with their ASCII rows still in (`b27ed4e`) were
compiled into the study under renamed classes and scored on the whole file of every scenario by a study-local
copy of `cosineDistance()` with the filter as a switch. 184 codec-cases — Russian counts three:

| table set | scoring | wrong | declined |
| --- | --- | --- | --- |
| original pair | unfiltered — the detector as it shipped | **184** | 0 |
| original pair | filtered | 0 | 112 — every Western European case, having no table |
| regenerated six | unfiltered | 168 | 0 |
| regenerated six | filtered | **0** | **0** |

The shipped detector was right on pure Russian prose and nothing else in the study, pure French, German,
Spanish and Polish prose included, all at full confidence. The filter alone rescues the original pair from
wrong to declined; the tables alone rescue only pure prose; both together are the current state.

On Russian, where the two sets can be compared directly, the regenerated 5,965-entry table beats the
original 14,662-entry one on every row: pure prose 0.02 with a margin of 0.92 against 0.12 and 0.81, and at
1% 0.41 / 0.54 against 0.44 / 0.50. Size did not help. One caveat: the Russian test prefix and the training
half are the same novel, so the regenerated table has an author's-vocabulary advantage on this test set that
the original does not. Finding 16 and the committed second author settle it: the verdict stands.

The experimental tables and the comparison code were not kept; the recipe is above, and the study's `judge()`
takes a scorer so the next comparison needs only the tables.

**16. The same two Russian tables on nineteen texts by other authors** — the original table's own training
set (the untracked `trigramfrequencytables/1/`, not committable: contemporary authors), which the regenerated
table has never seen. First 200 K characters of each, pure and as 1% lines in English prose, worst of the
three Cyrillic codecs, both tables scored with the filter. **Every case is correct for both tables.** Scores:

| | regenerated, single novel | original, trained on these texts |
| --- | --- | --- |
| pure prose | 0.08–0.24, margin 0.70–0.85 | 0.04–0.11, margin 0.81–0.88 |
| 1% lines | 0.43–0.72, margin 0.26–0.52 | 0.36–0.67, margin 0.30–0.58 |

Each table is ahead by 0.05–0.10 on the author it was trained on and behind by as much on the other's, so the
in-author advantage of finding 15 and this out-of-author deficit are the same effect from both sides; the
generalization gap of a one-novel table is smaller than either. Correctness is not at stake at these margins.
If the Russian score headroom is ever wanted — the worst case here is 0.72 against the 0.95 threshold — a
second and third public-domain author in `corpus/train/` is the lever, not table size.

The committed second author, Kuprin, scores 0.12 at 64 K under each of the three Cyrillic codecs, inside the
pure-prose range above; the tests assert it under 0.90.

**17. BOM-less UTF-16 and UTF-32 are told from binary by where their NUL bytes sit.** Counting NULs by offset
modulo 4 costs one pass, and only where a memchr has found a NUL at all. A wide text keeps its NULs in fixed
phases — UTF-16LE in the odd bytes (every one for Latin text, about a fifth for Cyrillic, whose zeros are its
spaces and punctuation), UTF-32 in two of every four — while binary has them everywhere. A phase counts as the
zero byte at 10% NUL and as a data byte under 1%. The layout alone is not enough: an array of small 16-bit
integers has UTF-16LE's layout exactly, and quiet 16-bit audio nearly, so the decoded candidate must also read
as text — at least three quarters letters, digits and whitespace (the corpus's lowest, the JSON host, is 85%)
and no more than one in a thousand control or replacement characters. Both synthetic arrays fail that on the
control characters alone. Every corpus text and both hosts decode exactly in all four wide encodings, with and
without a mark; a Windows-1251 text with one trailing NUL and the test executable are declined.

Found in passing: since the guard landed, a UTF-16 or UTF-32 file *with* a byte order mark had been declined
too, because the guard ran ahead of the mark check. The mark is now read first.

**18. The anchored sampler is in `decode()`, at a 256 KB budget.** `detect()` still scores whatever it is
given; `decode()` hands it `anchoredSample()` of the input — the input itself up to the budget, past it
128-byte chunks centred on evenly spaced non-ASCII bytes, found in two passes over the bytes so that a large
Cyrillic file never needs a list of its anchors. The winner then decodes the whole input once. The budget is
larger than the study needed: the 16 KB and 64 KB samples are both correct on all 140 scenarios, and 64 KB's
worst score sits 0.08 above the whole file's (Spanish at 1%, clustered: 0.65 against 0.57), so the extra
budget buys margin at a cost of some 35 ms per file. The study now calls the library's sampler at its own
budgets; its columns are unchanged except Polish prose, which at 24 KB fits the 64 KB budget and is scored
whole. The tests grow the sparsest scenarios (1% prose) and the densest (pure prose) to twice the budget by
repetition and require the same recovery under the same cap.

**19. The shortlist is eleven 8-bit codecs, and `detect()` no longer tries the Unicode ones.** Added:
ISO-8859-5, Windows-1252, ISO-8859-15, macintosh (Mac Roman) and Windows-1250; the corpus is lossless in all
of them, so every scenario now runs in every codec its language has — 392 codec-cases, whole and sampled,
none wrong. Removed: UTF-8, UTF-16 and UTF-32, which `decode()` settles before `detect()` runs and which
could only ever have produced a mangled runner-up. Mac Cyrillic and Mac Central European are not there
because Qt without ICU has no codec for them; a test now checks that every shortlist name resolves, since
`detect()` drops one it cannot without a word. Three things worth knowing:

- Windows-1252 and ISO-8859-15 write the corpus texts to the same bytes ISO-8859-1 does, and Windows-1250
  shares every Polish letter with ISO-8859-2 but ą, Ą, Ś and Ź. Codecs that read the same bytes as the same
  text tie on score, and the shortlist's order breaks the tie: the Windows one first, being the likelier.
- Because of that, the Polish *margin* — the winner against the closest reading that decodes differently —
  collapses from 0.66–0.81 to 0.01: the ISO-8859-2 and Windows-1250 readings of a Polish text differ in a few
  letters and score almost alike. The winner is still right on every Polish case in both encodings, whole and
  anchored; it is decided by the ą's. The margin was never a gate (finding 12), and this is one more reason it
  cannot be. The blind 16 KB spread sample, which ships nowhere, now picks the twin on three Polish 5% cases
  it used to get right: a window with too few ą's in it cannot tell them apart.
- Cost: the five added codecs each decode to a full text of letters, where the removed Unicode readings of
  8-bit bytes decoded to little that parsed. The slow route is ~20% dearer: 10.1 ms at 64 KB, 38.6 ms at
  256 KB, 48.8 ms at 768 KB.

### The design these point to

1. Score only trigrams carrying a non-ASCII character — done, in the library (finding 13).
2. Sample by anchoring fixed-size chunks on the non-ASCII bytes, at a bounded budget — done (finding 18).
3. Return nothing when the sample holds nothing to score, which the threshold already does.

## What the corpus changed

Worth recording, because it invalidated conclusions that looked solid.

Before the corpus existed, the study's "English" was the repository's own build output — `moc_*.cpp` and
`qrc_*.cpp` hex blobs — and its "Western European" was English with accents substituted in at a set rate. On
that data, whole-file detection appeared to *give up safely* on Western European, and a 64 KB sample in eight
chunks with 64 bytes of context around each anchor looked like the recommended design.

Both were wrong. Real English prose matches the English model far better than generated C++ does, which is
what lets the mangling-codec win outright; and real French, German, Spanish and Polish carry their accents in
distributions no substitution rate reproduces. The corrected answer — filter, do not merely sample — only
appeared once the corpus was real.

The lesson generalizes: **for an accuracy study, synthetic inputs are worth exactly as much as their realism,
and a synthetic input that is wrong in a way you have not thought of returns a confident wrong answer.**

## Known open items

- A binary file without a NUL byte runs the full detection. Above a few hundred bytes such files barely exist,
  and the outcome is a decline either way, so this is a cost, not a wrong answer.
- The text viewer's explicit "as UTF-8" action does not use the binary guard.
- Mac Cyrillic and Mac Central European text is read as its nearest listed neighbour, a decline or mojibake:
  Qt has no codec for either.
