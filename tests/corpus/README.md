# Test corpus

Real prose for the detector's tests, benchmarks and the window study. Committed rather than generated: text
drawn from the trigram tables and then scored against those same tables flatters the detector, and text walked
from whatever files happen to be on disk is not reproducible between machines.

All files are UTF-8 without a byte order mark. The tests re-encode them in memory into whichever 8-bit codec a
case calls for, so each file must survive that encoding unchanged - see the normalization note below.

| file | language | chars | non-ASCII | must be lossless in | source |
| --- | --- | --- | --- | --- | --- |
| `english.txt` | English | 728,697 | 0.00% | ASCII | *Pride and Prejudice*, Jane Austen, [PG 1342](https://www.gutenberg.org/ebooks/1342) |
| `french.txt` | French | 607,304 | 2.59% | ISO-8859-1 | *Le Fantôme de l'Opéra*, Gaston Leroux, [PG 62215](https://www.gutenberg.org/ebooks/62215) |
| `german.txt` | German | 468,893 | 1.96% | ISO-8859-1 | *Dr. Mabuse, der Spieler*, Norbert Jacques, [PG 50285](https://www.gutenberg.org/ebooks/50285) |
| `spanish.txt` | Spanish | 800,000 | 2.09% | ISO-8859-1 | *Don Quijote*, Cervantes, [PG 2000](https://www.gutenberg.org/ebooks/2000) |
| `polish.txt` | Polish | 240,484 | 6.81% | ISO-8859-2 | *Tajemnica Baskerville'ów*, Conan Doyle, tr. Żmijewska, [PG 34079](https://www.gutenberg.org/ebooks/34079) |
| `russian.txt` | Russian | 800,000 | 78.19% | Windows-1251, KOI8-R, CP866 | *Anna Karenina*, Tolstoy, supplied locally |

Between them they exercise every 8-bit codec in `CTextEncodingDetector::detect()`'s shortlist. Polish is the
only source of ISO-8859-2 coverage; English carries no non-ASCII at all, which is its purpose.

Russian does not come from Project Gutenberg, which holds nine Russian entries: the three prose works among
them are audiobooks carrying no text, leaving an arithmetic textbook and 18th-century odes. Neither resembles
modern Russian prose closely enough to draw trigram statistics from. *Anna Karenina* is supplied locally
instead, and `prepare_corpus.ps1` leaves `russian.txt` alone unless pointed at that source.

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

`prepare_corpus.ps1` downloads, strips, normalizes and verifies, and reproduces these files byte for byte. It
fails rather than writing a file that cannot survive its target codecs.

```
pwsh ./prepare_corpus.ps1                             # the Gutenberg five
pwsh ./prepare_corpus.ps1 -RussianSource <path>       # and russian.txt
```
