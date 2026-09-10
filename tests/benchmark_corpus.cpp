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

[[nodiscard]] const CorpusFile& fileFor(BenchmarkCorpus::Language language)
{
	return corpusFiles[static_cast<size_t>(language)];
}

}

const char* BenchmarkCorpus::name(Language language)
{
	return fileFor(language).name;
}

std::vector<const char*> BenchmarkCorpus::codecNames(Language language)
{
	return fileFor(language).codecNames;
}

const QString& BenchmarkCorpus::text(Language language)
{
	static std::array<QString, std::size(corpusFiles)> decoded;
	static std::array<bool, std::size(corpusFiles)> loaded{};

	const size_t index = static_cast<size_t>(language);
	if (!loaded[index])
	{
		loaded[index] = true;

		QFile file{ QStringLiteral(CORPUS_DIR "/test/") + QLatin1String(fileFor(language).fileName) };
		if (file.open(QIODevice::ReadOnly))
			decoded[index] = QString::fromUtf8(file.readAll()); // The corpus is UTF-8 without a byte order mark
	}

	return decoded[index];
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
