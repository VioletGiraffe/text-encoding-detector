# Corpus

Real prose in six languages, one work each, committed rather than generated: text drawn from the trigram
tables and then scored against those same tables flatters the detector, and text walked from whatever files
happen to be on disk is not reproducible between machines.

Each work is split in two, and the two halves never meet:

- `test/<language>.txt` - a prefix of the work, read by the tests, benchmarks and the window study
- `train/<language>.txt` - the remainder, which `text-analyzer` builds that language's trigram table from.
  English has no table: its trigrams are all ASCII, which the detector does not score, so `train/english.txt`
  is unused and kept only for the split's uniformity.

All files are UTF-8 without a byte order mark. The tests re-encode them in memory into whichever 8-bit codec a
case calls for, so each file must survive that encoding unchanged - see the normalization note below.

| language | test chars | train chars | non-ASCII | must be lossless in | source |
| --- | --- | --- | --- | --- | --- |
| English | 200,000 | 528,697 | 0.00% | ASCII | *Pride and Prejudice*, Jane Austen, [PG 1342](https://www.gutenberg.org/ebooks/1342) |
| French | 150,000 | 457,304 | 2.59% | ISO-8859-1 | *Le Fantôme de l'Opéra*, Gaston Leroux, [PG 62215](https://www.gutenberg.org/ebooks/62215) |
| German | 100,000 | 368,893 | 1.96% | ISO-8859-1 | *Dr. Mabuse, der Spieler*, Norbert Jacques, [PG 50285](https://www.gutenberg.org/ebooks/50285) |
| Spanish | 200,000 | 600,000 | 2.09% | ISO-8859-1 | *Don Quijote*, Cervantes, [PG 2000](https://www.gutenberg.org/ebooks/2000) |
| Polish | 24,000 | 216,484 | 6.81% | ISO-8859-2 | *Tajemnica Baskerville'ów*, Conan Doyle, tr. Żmijewska, [PG 34079](https://www.gutenberg.org/ebooks/34079) |
| Russian | 800,000 | 912,686 | 78.08% | Windows-1251, KOI8-R, CP866 | *Anna Karenina*, Tolstoy, supplied locally |

Two more test-only files are *hosts*: ASCII text the window study sprinkles the languages into, alongside the
English prose. No table is built from them, and they must hold no non-ASCII byte at all.

| file | chars | source |
| --- | --- | --- |
| `test/code.txt` | 200,000 | the first 200,000 characters of `sqlite3.c`, the SQLite 3.53.4 amalgamation (public domain) |
| `test/json.txt` | 200,000 | the first 200,000 characters of a packet-capture log exported as JSON, supplied locally |

The test prefix is at least a tenth of the shortest work and larger where the work affords it: the English one
is the ASCII half of every mixed-content study scenario, the Russian one spans the benchmark ladder. The
Gutenberg works are capped at 800,000 characters; Russian is not, being the one work not bounded by its
download and the language with the most encodings to tell apart.

Between them they exercise every 8-bit codec in `CTextEncodingDetector::detect()`'s shortlist. Polish is the
only source of ISO-8859-2 coverage; English carries no non-ASCII at all, which is its purpose.

Russian does not come from Project Gutenberg, which holds nine Russian entries: the three prose works among
them are audiobooks carrying no text, leaving an arithmetic textbook and 18th-century odes. Neither resembles
modern Russian prose closely enough to draw trigram statistics from. *Anna Karenina* is supplied locally
instead, and `prepare_corpus.ps1` leaves the russian files alone unless pointed at that source.

## Licence

The five Project Gutenberg works are in the public domain in the United States, which is the basis on which PG
distributes them. The PG wrapper around each is not public domain and is stripped, leaving the work itself.
*Anna Karenina* was published in 1878 and is likewise public domain.

## Normalization

Two changes are made to each download, both so that encoding into the target codec loses nothing. A character
the codec cannot represent encodes as `?` - an ASCII byte, which distorts the trigram statistics and acts as
ASCII filler inside text that is supposed to be non-ASCII.

- Typographic characters no 8-bit target carries are replaced by the plain forms they stand for:
  curly quotes, en and em dashes, the ellipsis, and the `oe` ligature.
- Anything still unrepresentable in *every* target codec of that file is dropped. This covers 33 characters in
  the Polish scan, including a handful of stray Cyrillic letters that would otherwise poison a
  Latin-against-Cyrillic test.

The Cyrillic codecs are the strict ones, so Russian needs the most work: KOI8-R and CP866 carry neither
guillemets nor an en dash, which Russian prose uses constantly, and no accented Latin at all — *Anna Karenina*
runs to some ninety accented characters of French dialogue, which are dropped. Guillemets become `"` and `№`
becomes `No.`.

English is reduced to pure ASCII deliberately: its role is the codec-independent half of a mixed-content file,
where a single non-ASCII character would be a false anchor for a sampler that seeks them out.

## Regenerating

`prepare_corpus.ps1` downloads, strips, normalizes, verifies and splits, and reproduces these files byte for
byte. It fails rather than writing a file that cannot survive its target codecs.

```
pwsh ./prepare_corpus.ps1                                             # the Gutenberg five
pwsh ./prepare_corpus.ps1 -RussianSource <path>                       # and the russian files
pwsh ./prepare_corpus.ps1 -CodeSource <sqlite3.c> -JsonSource <log>   # and the hosts
```

The trigram tables are regenerated from the training halves, one language per run, from the directory the
generated files live in. The optional last argument is the minimum occurrence count a trigram needs to be
kept, 10 by default; all-ASCII trigrams are left out regardless:

```
cd ../text-encoding-detector/src/trigramfrequencytables
text_analyzer French ../../../corpus/train/french.txt 10
```
