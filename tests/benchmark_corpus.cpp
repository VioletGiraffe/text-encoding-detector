#include "benchmark_corpus.h"

DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
#include <QFile>
#include <QTextCodec>
RESTORE_COMPILER_WARNINGS

#include <memory>

QString BenchmarkCorpus::filePath()
{
	const QByteArray fromEnvironment = qgetenv("TEXT_ENCODING_DETECTOR_CORPUS");
	return fromEnvironment.isEmpty() ? QStringLiteral(DEFAULT_CORPUS_FILE) : QString::fromLocal8Bit(fromEnvironment);
}

QByteArray BenchmarkCorpus::codecName()
{
	const QByteArray fromEnvironment = qgetenv("TEXT_ENCODING_DETECTOR_CORPUS_CODEC");
	return fromEnvironment.isEmpty() ? QByteArrayLiteral("KOI8-R") : fromEnvironment;
}

std::string BenchmarkCorpus::missingCorpusMessage()
{
	return "No corpus at " + filePath().toStdString()
		+ " - point TEXT_ENCODING_DETECTOR_CORPUS at a text file and TEXT_ENCODING_DETECTOR_CORPUS_CODEC at its encoding";
}

const QString& BenchmarkCorpus::text()
{
	static const QString corpus = []() -> QString {
		QFile file{ filePath() };
		if (!file.open(QIODevice::ReadOnly))
			return {};

		QTextCodec* const codec = QTextCodec::codecForName(codecName());
		if (!codec)
			return {};

		const std::unique_ptr<QTextDecoder> decoder{ codec->makeDecoder(QTextCodec::IgnoreHeader) };
		return decoder->toUnicode(file.readAll());
	}();

	return corpus;
}

QString BenchmarkCorpus::slice(qsizetype characters)
{
	QString sliced = text().left(characters);
	if (!sliced.isEmpty() && sliced.back().isHighSurrogate()) // A lone surrogate encodes as a replacement character
		sliced.chop(1);

	return sliced;
}

QByteArray BenchmarkCorpus::encoded(const char* targetCodecName, qsizetype characters)
{
	QTextCodec* const codec = QTextCodec::codecForName(targetCodecName);
	if (text().isEmpty() || !codec)
		return {};

	const std::unique_ptr<QTextEncoder> encoder{ codec->makeEncoder(QTextCodec::IgnoreHeader) };
	return encoder->fromUnicode(slice(characters));
}

QByteArray BenchmarkCorpus::executableBytes(qsizetype bytes)
{
	static const QByteArray image = []() -> QByteArray {
		QFile file{ QCoreApplication::applicationFilePath() };
		return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
	}();

	return image.left(bytes);
}
