# Rebuilds the committed corpus files from their sources.
#
# The committed .txt files are what the tests and text-analyzer read; this script exists so that what was done
# to them is on the record and can be repeated. Running it should reproduce the committed files byte for byte.
#
#   pwsh ./prepare_corpus.ps1                                  # the Gutenberg five and the Wikisource one
#   pwsh ./prepare_corpus.ps1 -RussianSource <path to Anna Karenina .txt>
#   pwsh ./prepare_corpus.ps1 -CodeSource <path to sqlite3.c> -JsonSource <path to the packet log>
#   pwsh ./prepare_corpus.ps1 -OutputDir <path>
#
# Each work is split in two: test/<file> is a prefix the tests read, train/<file> is the remainder text-analyzer
# builds the trigram tables from. Nothing scored by the tests has been seen by the tables. A work with test = 0
# is all test: a second author in a language, there to show what the table built from the first generalizes to.
#
# The hosts, code.txt and json.txt, are ASCII files the study sprinkles the languages into. They are test-only,
# supplied locally like Russian, and must hold no non-ASCII byte: one would be a false anchor for a sampler that
# seeks them out.
#
# Five works come from Project Gutenberg. The sixth, Russian, does not: Gutenberg holds nine Russian entries
# and the three prose works among them are audiobooks with no text, leaving only an arithmetic textbook and
# 18th-century odes. Anna Karenina is public domain but has to be supplied locally, so -RussianSource names it
# and the russian files are left alone when it is absent. Kuprin's Poedinok comes from Wikisource, a chapter per
# page, each fetched at a pinned revision: the pages stay editable, and the script has to reproduce the file.
#
# Two transformations are applied, both so that encoding into the target 8-bit codecs loses nothing. A
# character a codec cannot represent encodes as '?' - an ASCII byte, which distorts the trigram statistics and
# acts as ASCII filler inside text meant to be non-ASCII.
#   - typographic characters no 8-bit target carries are replaced by the plain forms they stand for
#   - anything still unrepresentable in any one of the file's target codecs is dropped
#
# The Cyrillic codecs are the strict ones: KOI8-R and CP866 carry neither guillemets nor an en dash, both of
# which Russian prose uses constantly, and no accented Latin at all - Anna Karenina's French dialogue.
#
# English is reduced to pure ASCII deliberately. Its role is the codec-independent half of a mixed-content
# file, and a single non-ASCII character in it would act as a false anchor for a sampler that seeks them out.

param(
    [string]$OutputDir = $PSScriptRoot,
    [string]$RussianSource = '',
    [string]$CodeSource = '',
    [string]$JsonSource = ''
)

$ErrorActionPreference = 'Stop'
[System.Text.Encoding]::RegisterProvider([System.Text.CodePagesEncodingProvider]::Instance)

# max: the character cap on the whole work, 0 for none. Russian is uncapped: the one work not bounded by its
# download, and the language with the most encodings to tell apart.
# test: the prefix the tests read, 0 for the whole work. At least a tenth of the shortest work, more where the
# work affords it; the English one is the ASCII half of every mixed-content scenario, the Russian one spans the
# benchmark ladder.
# id: a Project Gutenberg ebook. revisions: Wikisource page revisions, one per chapter, in order. Neither: local.
$cyrillicExtra = [ordered]@{ ([char]0x00AB) = '"'; ([char]0x00BB) = '"'; ([char]0x2116) = 'No.' }
$sources = @(
    @{ file = 'english.txt'; id = 1342;  max = 800000; test = 200000; codepages = @();                 codecs = 'ASCII' }
    @{ file = 'french.txt';  id = 62215; max = 800000; test = 150000; codepages = @(28591);            codecs = 'ISO-8859-1' }
    @{ file = 'german.txt';  id = 50285; max = 800000; test = 100000; codepages = @(28591);            codecs = 'ISO-8859-1' }
    @{ file = 'spanish.txt'; id = 2000;  max = 800000; test = 200000; codepages = @(28591);            codecs = 'ISO-8859-1' }
    @{ file = 'polish.txt';  id = 34079; max = 800000; test = 24000;  codepages = @(28592);            codecs = 'ISO-8859-2' }
    @{ file = 'russian.txt'; max = 0; test = 800000; codepages = @(1251, 20866, 866); codecs = 'Windows-1251, KOI8-R, CP866'; extra = $cyrillicExtra }
    @{ file = 'russian-kuprin.txt'; max = 0; test = 0; codepages = @(1251, 20866, 866); codecs = 'Windows-1251, KOI8-R, CP866'; extra = $cyrillicExtra
       revisions = @(1360689, 1213688, 1360690, 1360691, 5063471, 1214114, 1360692, 1214373, 5062977, 5351114, 1360764, 1214514,
                     5062978, 1360765, 5062979, 1360766, 1215146, 5062980, 5062982, 1215232, 1215267, 1215719, 1215721) }
)

$userAgent = 'text-encoding-detector corpus script (https://github.com/VioletGiraffe/text-encoding-detector)'

# The chapter text out of a Wikisource page: the page's own template header, the editors' footnotes and section
# headings go, formatting templates and tags leave their text behind. Fails on any markup it does not know.
function ConvertFrom-Wikitext([string]$wikitext) {
    $text = [regex]::Replace($wikitext, '(?s)\A\{\{.*?\n\}\}\n', '')
    $text = [regex]::Replace($text, '(?s)<ref>.*?</ref>', '')
    $text = [regex]::Replace($text, '(?m)^==.*==$', '')
    $text = [regex]::Replace($text, '\{\{(fsp|нп)\}\}', ' ')                                            # thin and non-breaking spaces
    $text = [regex]::Replace($text, '\{\{(Акут|roman\|\d+|---[^{}]*|примечания|PD-old-70)\}\}', '')     # stress marks, chapter numbers, rules, licence
    while ($text -match '\{\{[^{}]*\|[^{}]*\}\}') {                                                     # razr, right2, lang: the text stays
        $text = [regex]::Replace($text, '\{\{[^{}|]*\|(?:[a-z-]+\|)?([^{}]*)\}\}', '$1')
    }
    $text = [regex]::Replace($text, '<[^>]+>', '')
    $text = $text.Replace("'''", '').Replace("''", '')

    $left = [regex]::Match($text, '.{0,30}(\{\{|\}\}|<|\[\[).{0,30}')
    if ($left.Success) { throw "unhandled wikitext markup: $($left.Value)" }

    $text = [regex]::Replace($text, '[ \t]+\n', "`n")
    return [regex]::Replace($text, '\n{3,}', "`n`n").Trim()
}

foreach ($subdir in 'test', 'train') { New-Item -ItemType Directory -Force (Join-Path $OutputDir $subdir) | Out-Null }

$replacements = [ordered]@{
    ([char]0x201C) = '"'; ([char]0x201D) = '"'; ([char]0x201E) = '"'; ([char]0x2018) = "'"; ([char]0x2019) = "'"
    ([char]0x2014) = '-'; ([char]0x2013) = '-'; ([char]0x2026) = '...'; ([char]0x0153) = 'oe'; ([char]0x0152) = 'OE'
}

foreach ($source in $sources) {
    if ($source.Contains('revisions')) {
        $chapters = foreach ($revision in $source.revisions) {
            $url = "https://ru.wikisource.org/w/index.php?oldid=$revision&action=raw"
            $download = Join-Path ([System.IO.Path]::GetTempPath()) "ws$revision.wiki"
            & curl.exe -sSL --fail --max-time 120 -A $userAgent -o $download $url
            if ($LASTEXITCODE -ne 0) { throw "$($source.file): download failed from $url" }

            $wikitext = [System.IO.File]::ReadAllText($download, [System.Text.Encoding]::UTF8)
            Remove-Item $download -Force
            ConvertFrom-Wikitext $wikitext.Replace("`r`n", "`n")
        }

        $body = $chapters -join "`n`n"
    }
    elseif (-not $source.Contains('id')) {
        if ([string]::IsNullOrEmpty($RussianSource)) { "{0,-18} skipped: pass -RussianSource to rebuild it" -f $source.file; continue }
        $body = [System.IO.File]::ReadAllText($RussianSource, [System.Text.Encoding]::UTF8)
    }
    else {
        $url = "https://www.gutenberg.org/ebooks/$($source.id).txt.utf-8"

        # curl rather than Invoke-WebRequest: Gutenberg answers the latter with a 302 it will not follow, and a
        # download to file keeps the bytes exactly as served
        $download = Join-Path ([System.IO.Path]::GetTempPath()) "pg$($source.id).txt"
        & curl.exe -sSL --fail --max-time 120 -A 'Mozilla/5.0' -o $download $url
        if ($LASTEXITCODE -ne 0) { throw "$($source.file): download failed from $url" }

        $raw = [System.IO.File]::ReadAllText($download, [System.Text.Encoding]::UTF8)
        Remove-Item $download -Force

        $startMatch = [regex]::Match($raw, '\*\*\*\s*START OF TH[EIS]+ PROJECT GUTENBERG EBOOK.*?\*\*\*')
        $endMatch = [regex]::Match($raw, '\*\*\*\s*END OF TH[EIS]+ PROJECT GUTENBERG EBOOK.*?\*\*\*')
        if (-not $startMatch.Success -or -not $endMatch.Success) { throw "$($source.file): Project Gutenberg markers not found" }

        $from = $startMatch.Index + $startMatch.Length
        $body = $raw.Substring($from, $endMatch.Index - $from)
    }

    # LF, which is what .gitattributes keeps both in the repository and on disk: Gutenberg serves CRLF, and a
    # file that arrives one way and is committed the other cannot be reproduced by this script
    $body = $body.Replace("`r`n", "`n").Trim()

    $codecs = @($source.codepages | ForEach-Object { [System.Text.Encoding]::GetEncoding($_) })
    $carries = {
        param($ch)
        if ($codecs.Count -eq 0) { return [int]$ch -lt 0x80 }
        foreach ($codec in $codecs) { if ($codec.GetString($codec.GetBytes([string]$ch)) -ne [string]$ch) { return $false } }
        return $true
    }

    $builder = New-Object System.Text.StringBuilder
    foreach ($ch in $body.ToCharArray()) {
        if ($null -ne $source.extra -and $source.extra.Contains($ch)) { [void]$builder.Append($source.extra[$ch]) }
        elseif ($replacements.Contains($ch)) { [void]$builder.Append($replacements[$ch]) }
        elseif ([int]$ch -lt 0x80 -or (& $carries $ch)) { [void]$builder.Append($ch) }
    }

    $text = $builder.ToString()
    if ($source.max -gt 0 -and $text.Length -gt $source.max) { $text = $text.Substring(0, $source.max) }

    # The point of the exercise: the text must survive every one of its target codecs unchanged
    $nonAscii = 0
    foreach ($ch in $text.ToCharArray()) { if ([int]$ch -ge 0x80) { $nonAscii++ } }

    $lost = 0
    if ($codecs.Count -eq 0) { $lost = $nonAscii }
    else {
        foreach ($codec in $codecs) {
            $roundTrip = $codec.GetString($codec.GetBytes($text))
            for ($i = 0; $i -lt $text.Length; $i++) { if ($roundTrip[$i] -ne $text[$i]) { $lost++ } }
        }
    }

    if ($lost -ne 0) { throw "$($source.file): $lost characters cannot be represented in $($source.codecs)" }

    $utf8 = New-Object System.Text.UTF8Encoding($false)
    if ($source.test -eq 0) {
        [System.IO.File]::WriteAllText((Join-Path $OutputDir 'test' $source.file), $text, $utf8)
        "{0,-18} {1,8} chars: all test; non-ASCII {2,5:N2}%, lossless in {3}" -f $source.file, $text.Length, (100 * $nonAscii / $text.Length), $source.codecs
        continue
    }

    if ($text.Length -le $source.test) { throw "$($source.file): $($text.Length) characters leave nothing to train on after a $($source.test) test prefix" }

    [System.IO.File]::WriteAllText((Join-Path $OutputDir 'test' $source.file), $text.Substring(0, $source.test), $utf8)
    [System.IO.File]::WriteAllText((Join-Path $OutputDir 'train' $source.file), $text.Substring($source.test), $utf8)

    "{0,-18} {1,8} chars: test {2,7}, train {3,8}; non-ASCII {4,5:N2}%, lossless in {5}" -f `
        $source.file, $text.Length, $source.test, ($text.Length - $source.test), (100 * $nonAscii / $text.Length), $source.codecs
}

$hosts = @(
    @{ file = 'code.txt'; source = $CodeSource; param = 'CodeSource'; chars = 200000 }
    @{ file = 'json.txt'; source = $JsonSource; param = 'JsonSource'; chars = 200000 }
)

foreach ($hostFile in $hosts) {
    if ([string]::IsNullOrEmpty($hostFile.source)) { "{0,-18} skipped: pass -{1} to rebuild it" -f $hostFile.file, $hostFile.param; continue }

    $text = [System.IO.File]::ReadAllText($hostFile.source, [System.Text.Encoding]::UTF8).Replace("`r`n", "`n").Substring(0, $hostFile.chars)
    foreach ($ch in $text.ToCharArray()) { if ([int]$ch -ge 0x80) { throw "$($hostFile.file): the source is not pure ASCII" } }

    [System.IO.File]::WriteAllText((Join-Path $OutputDir 'test' $hostFile.file), $text, (New-Object System.Text.UTF8Encoding($false)))
    "{0,-18} {1,8} chars: test {1,7}, host, ASCII" -f $hostFile.file, $text.Length
}
