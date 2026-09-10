#include "benchmark_corpus.h"

DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
#include <QFile>
#include <QTextCodec>
RESTORE_COMPILER_WARNINGS

#include <array>
#include <memory>

namespace {

struct CorpusFile
{
	const char* name;
	const char* fileName;
	std::vector<const char*> codecNames;
};

// The codecs each text is lossless in, which prepare_corpus.ps1 enforces when it writes the file
const std::array corpusFiles {
	CorpusFile{ "english", "english.txt", {} },
	CorpusFile{ "french",  "french.txt",  { "ISO-8859-1" } },
	CorpusFile{ "german",  "german.txt",  { "ISO-8859-1" } },
	CorpusFile{ "spanish", "spanish.txt", { "ISO-8859-1" } },
	CorpusFile{ "polish",  "polish.txt",  { "ISO-8859-2" } },
	CorpusFile{ "russian", "russian.txt", { "Windows-1251", "KOI8-R", "CP866" } },
};

const std::array hostFiles {
	CorpusFile{ "code", "code.txt", {} },
	CorpusFile{ "json", "json.txt", {} },
};

[[nodiscard]] const CorpusFile& fileFor(BenchmarkCorpus::Language language)
{
	return corpusFiles[static_cast<size_t>(language)];
}

[[nodiscard]] const CorpusFile& fileFor(BenchmarkCorpus::Host host)
{
	return hostFiles[static_cast<size_t>(host)];
}

[[nodiscard]] QString readTestFile(const char* fileName)
{
	QFile file{ QStringLiteral(CORPUS_DIR "/test/") + QLatin1String(fileName) };
	return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString{}; // The corpus is UTF-8 without a byte order mark
}

// Each file read once, on first use
template <size_t N>
[[nodiscard]] const QString& cachedText(std::array<QString, N>& decoded, std::array<bool, N>& loaded, size_t index, const char* fileName)
{
	if (!loaded[index])
	{
		loaded[index] = true;
		decoded[index] = readTestFile(fileName);
	}

	return decoded[index];
}

}

const char* BenchmarkCorpus::name(Language language)
{
	return fileFor(language).name;
}

const char* BenchmarkCorpus::name(Host host)
{
	return fileFor(host).name;
}

std::vector<const char*> BenchmarkCorpus::codecNames(Language language)
{
	return fileFor(language).codecNames;
}

const QString& BenchmarkCorpus::text(Language language)
{
	static std::array<QString, std::size(corpusFiles)> decoded;
	static std::array<bool, std::size(corpusFiles)> loaded{};
	return cachedText(decoded, loaded, static_cast<size_t>(language), fileFor(language).fileName);
}

const QString& BenchmarkCorpus::text(Host host)
{
	static std::array<QString, std::size(hostFiles)> decoded;
	static std::array<bool, std::size(hostFiles)> loaded{};
	return cachedText(decoded, loaded, static_cast<size_t>(host), fileFor(host).fileName);
}

QString BenchmarkCorpus::slice(Language language, qsizetype characters)
{
	QString sliced = text(language).left(characters);
	if (!sliced.isEmpty() && sliced.back().isHighSurrogate()) // A lone surrogate encodes as a replacement character
		sliced.chop(1);

	return sliced;
}

QByteArray BenchmarkCorpus::encoded(Language language, const char* codecName, qsizetype characters)
{
	QTextCodec* const codec = QTextCodec::codecForName(codecName);
	if (!codec)
		return {};

	const std::unique_ptr<QTextEncoder> encoder{ codec->makeEncoder(QTextCodec::IgnoreHeader) };
	return encoder->fromUnicode(slice(language, characters));
}

QString BenchmarkCorpus::representable(Language language, const char* codecName, qsizetype characters)
{
	QTextCodec* const codec = QTextCodec::codecForName(codecName);
	if (!codec)
		return {};

	const std::unique_ptr<QTextDecoder> decoder{ codec->makeDecoder(QTextCodec::IgnoreHeader) };
	return decoder->toUnicode(encoded(language, codecName, characters));
}

QByteArray BenchmarkCorpus::executableBytes(qsizetype bytes)
{
	static const QByteArray image = []() -> QByteArray {
		QFile file{ QCoreApplication::applicationFilePath() };
		return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
	}();

	return image.left(bytes);
}
