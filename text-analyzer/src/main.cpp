#include "ctextparser.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
RESTORE_COMPILER_WARNINGS

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
};

CTextParser::OccurrenceTable buildTable()
{
	static const Trigram trigrams[] = {
%3
		{nullptr, 0},
	};

	CTextParser::OccurrenceTable table;
	table.trigramOccurrenceTable.reserve(std::size(trigrams));

	uint64_t totalCount = 0;
	for (quint64 i = 0; trigrams[i].trigram != nullptr; ++i)
	{
		const QString trigramString = QString::fromUtf8(trigrams[i].trigram);
		table.trigramOccurrenceTable.try_emplace(
			CTextParser::OccurrenceTable::Trigram{ trigramString[0], trigramString[1], trigramString[2] },
			CTextParser::OccurrenceTable::Stats{ trigrams[i].rawCount }
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
	std::cout << "text_analyzer <language name> <path to a text file, or to a folder of .txt files> [minimum occurrences, default 10]" << std::endl;
	std::cout << "Text files must be encoded in UTF-8. Trigrams seen fewer times than the minimum are left out of the table." << std::endl;
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
	if (argc != 3 && argc != 4)
	{
		printUsageInstructions();
		return -1;
	}

	const QString languageName(argv[1]);
	const quint64 minimumOccurrences = argc == 4 ? QString{ argv[3] }.toULongLong() : 10;

	CTextParser parser;
	if (const QFileInfo input{ QString{ argv[2] } }; input.isFile())
		parseFile(input, parser);
	else
		scanFolder(input.absoluteFilePath(), parser);

	const QString className = QString("CTrigramFrequencyTable_") + languageName;
	const QString headerFileName = className.toLower() + ".h";
	const QString cppFileName = className.toLower() + ".cpp";

	QFile outputFile(headerFileName);
	if (!outputFile.open(QFile::WriteOnly))
	{
		std::cout << "Failed to write " << headerFileName.toStdString() << std::endl;
		return -1;
	}

	QTextStream stream(&outputFile);
	stream.setEncoding(QStringConverter::Utf8);
	stream.setGenerateByteOrderMark(false);

	stream << tableClassHeaderTemplate.arg(languageName);

	outputFile.close();

	outputFile.setFileName(cppFileName);
	if (!outputFile.open(QFile::WriteOnly))
	{
		std::cout << "Failed to write " << cppFileName.toStdString() << std::endl;
		return -1;
	}

	QString constructorBody;
	const QString constructorLineTemplate("\t\t{\"%1\", %2ULL},\n");

	// All-ASCII trigrams are never scored: they decode the same under every 8-bit codec
	std::vector<std::pair<QString, CTextParser::OccurrenceTable::Stats>> sortedTable;
	for (const auto& pair : parser.parsingResult().trigramOccurrenceTable)
	{
		if (pair.first.hasNonAsciiCharacter())
			sortedTable.emplace_back(pair.first.toString(), pair.second);
	}

	// Sort from higher to lower occurrence
	std::ranges::sort(sortedTable, std::greater<>(), [](const auto& pair) { return pair.second.rawCount; });

	// The vector is now sorted, find where the count drops below the minimum and cut this tail
	auto it = std::find_if(sortedTable.begin(), sortedTable.end(), [minimumOccurrences](const auto& pair) { return pair.second.rawCount < minimumOccurrences; });
	sortedTable.erase(it, sortedTable.end());

	for (const auto& trigram : sortedTable)
		constructorBody.append(constructorLineTemplate.arg(trigram.first).arg(trigram.second.rawCount));

	stream << tableClassCppTemplate.arg(headerFileName).arg(languageName).arg(constructorBody);

	return 0;
}
