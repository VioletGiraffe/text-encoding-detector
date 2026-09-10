#include "ctextparser.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
RESTORE_COMPILER_WARNINGS

#include <assert.h>
#include <algorithm>
#include <iostream>
#include <utility>

static const QString tableClassHeaderTemplate = R"(#pragma once

#include "ctrigramfrequencytable_base.h"

class CTrigramFrequencyTable_%1 final : public CTrigramFrequencyTable_Base
{
public:
	CTrigramFrequencyTable_%1() noexcept;

	[[nodiscard]] inline QString language() const override { return "%1"; }
};
)";

static const QString tableClassCppTemplate = R"(
#include "%1"

namespace {
struct Trigram {
	const char* trigram;
	quint64 rawCount = 0;
	float loss = 0.0f;
};

CTextParser::OccurrenceTable buildTable()
{
	static const Trigram trigrams[] = {
%3
		{nullptr, 0, 0.0f},
	};

	CTextParser::OccurrenceTable table;
	table.trigramOccurrenceTable.reserve(std::size(trigrams));

	uint64_t totalCount = 0;
	for (quint64 i = 0; trigrams[i].trigram != nullptr; ++i)
	{
		const QString trigramString = QString::fromUtf8(trigrams[i].trigram);
		table.trigramOccurrenceTable.try_emplace(
			CTextParser::OccurrenceTable::Trigram{ trigramString[0], trigramString[1], trigramString[2] },
			CTextParser::OccurrenceTable::Stats{ trigrams[i].rawCount, trigrams[i].loss }
		);
		totalCount += trigrams[i].rawCount;
	}

	table.totalTrigramsCount = totalCount;
	return table;
}
}

CTrigramFrequencyTable_%2::CTrigramFrequencyTable_%2() noexcept
	: CTrigramFrequencyTable_Base(buildTable())
{
}
)";

static void printUsageInstructions()
{
	std::cout << "Usage:" << std::endl;
	std::cout << "text_analyzer <language name> <path to a text file, or to a folder of .txt files>" << std::endl;
	std::cout << "Text files must be encoded in UTF-8." << std::endl;
	std::cout << std::endl;
	std::cout << "Output: ctrigramfrequencytable_<Language name>.h and ctrigramfrequencytable_<Language name>.cpp source files in the working directory, containing the declaration and definition of the CTrigramFrequencyTable_<Language name> class." << std::endl;
}

static void parseFile(const QFileInfo& entry, CTextParser& parser)
{
	QFile file{ entry.absoluteFilePath() };
	if (!file.open(QFile::ReadOnly))
	{
		std::cout << "Failed to open " << entry.fileName().toStdString() << std::endl;
		return;
	}

	// Invalid UTF-8 decodes to replacement characters: parse() skips them as non-letters and joins the letters around them into trigrams the text never had
	const QString text = QString::fromUtf8(file.readAll());
	if (text.contains(QChar{ QChar::ReplacementCharacter }) || !parser.parse(text))
	{
		std::cout << "Failed to parse " << entry.fileName().toStdString() << std::endl;
		std::cout << "Make sure it's a UTF-8 text file." << std::endl;
	}
}

// Iterate the folder, scan all .txt files
static void scanFolder(const QString& folderPath, CTextParser& parser)
{
	QDir folder(folderPath);
	folder.setFilter(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

	const auto entryList = folder.entryInfoList();
	for (const QFileInfo& entry : entryList)
	{
		if (entry.isDir())
			scanFolder(entry.absoluteFilePath(), parser);
		else if (entry.isFile() && entry.suffix().toLower() == "txt")
			parseFile(entry, parser);
	}
}

int main(int argc, char* argv[])
{
	if (argc != 3)
	{
		printUsageInstructions();
		return -1;
	}

	const QString languageName(argv[1]);

	CTextParser parser;
	if (const QFileInfo input{ QString{ argv[2] } }; input.isFile())
		parseFile(input, parser);
	else
		scanFolder(input.absoluteFilePath(), parser);

	parser.calculateLoss();

	const QString className = QString("CTrigramFrequencyTable_") + languageName;
	const QString headerFileName = className.toLower() + ".h";
	const QString cppFileName = className.toLower() + ".cpp";

	QFile outputFile(headerFileName);
	outputFile.open(QFile::WriteOnly);
	QTextStream stream(&outputFile);
	stream.setEncoding(QStringConverter::Utf8);
	stream.setGenerateByteOrderMark(false);

	stream << tableClassHeaderTemplate.arg(languageName);

	outputFile.close();

	outputFile.setFileName(cppFileName);
	outputFile.open(QFile::WriteOnly);

	QString constructorBody;
	const QString constructorLineTemplate("\t\t{\"%1\", %2ULL, %3f},\n");

	std::vector<std::pair<QString, CTextParser::OccurrenceTable::Stats>> sortedTable;
	for (const auto& pair : parser.parsingResult().trigramOccurrenceTable)
		sortedTable.emplace_back(pair.first.toString(), pair.second);

	// Sort from higher to lower occurrence
	std::ranges::sort(sortedTable, std::greater<>(), [](const auto& pair) { return pair.second.rawCount; });

	static constexpr quint64 Threshold = 10; // Ignore trigrams that occur less than this number of times
	// The vector is now sorted, find where the count drops below the threshold and cut this tail (resize)
	auto it = std::find_if(sortedTable.begin(), sortedTable.end(), [](const auto& pair) { return pair.second.rawCount < Threshold; });
	sortedTable.erase(it, sortedTable.end());

	for (size_t i = 0, N = sortedTable.size(); i < N; ++i)
	{
		const auto& trigram = sortedTable[i];
		QString lossString = QString::number(trigram.second.loss, 'g', 6);
		if (!lossString.contains('.') && !lossString.contains('e'))
			lossString += ".0";
		constructorBody.append(constructorLineTemplate.arg(trigram.first).arg(trigram.second.rawCount).arg(lossString));
	}

	stream << tableClassCppTemplate.arg(headerFileName).arg(languageName).arg(constructorBody);

	return 0;
}
