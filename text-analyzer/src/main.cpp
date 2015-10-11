#include "ctextparser.h"

#include <QDebug>
#include <QTextStream>
#include <QFile>

#include <assert.h>
#include <algorithm>
#include <utility>

static const QString tableClassHeaderTemplate =
	"#pragma once\n \
\n\
#include \"ctrigramfrequencytable_base.h\"\n\
\n\
class CTrigramFrequencyTable_%1 : public CTrigramFrequencyTable_Base\n\
{\n\
public:\n\
	CTrigramFrequencyTable_%1();\n\
\n\
	inline QString language() const override {return \"%1\";}\
\n\
};\n";

static const QString tableClassCppTemplate =
	"#include \"%1\"\n\
\n\
\n\
struct Trigram {\n\
\tconst char * trigram;\n\
\tquint64 count;\n\
};\n\
\n\
CTrigramFrequencyTable_%2::CTrigramFrequencyTable_%2()\n\
{\n\
\t_table.totalTrigrammsCount = %3ull;\n\
\tstatic const Trigram trigrams[] = {\n\
%4\
\t\t{nullptr, 0},\n\
\t};\n\
\n\
\tfor (quint64 i = 0; trigrams[i].trigram != nullptr; ++i)\n\
\t\t_table.trigramOccurrenceTable[QString::fromUtf8(trigrams[i].trigram)] = trigrams[i].count;\n\
}\n";

template<typename A, typename B>
std::multimap<B,A> flip_map(const std::map<A,B> &src)
{
	std::multimap<B,A> dst;
	std::transform(src.begin(), src.end(), std::inserter(dst, dst.begin()), [](const std::pair<A,B> &p){return std::make_pair(p.second, p.first);});
	return dst;
}

void main(int argc, char *argv[])
{
	if (argc < 3)
		return;

	const QString languageName(argv[1]);

	CTextParser parser;
	for (int i = 2; i < argc; ++i)
	{
		if (!parser.parse(QString(argv[i]), "UTF-8"))
			qDebug() << "Failed to parse" << argv[i];
	}

	const QString className = QString("CTrigramFrequencyTable_") + languageName;
	const QString headerFileName = className.toLower() + ".h";
	const QString cppFileName = className.toLower() + ".cpp";

	QFile outputFile(headerFileName);
	outputFile.open(QFile::WriteOnly);
	QTextStream stream(&outputFile);
	stream.setCodec("UTF-8");
	stream.setGenerateByteOrderMark(false);

	stream << tableClassHeaderTemplate.arg(languageName);

	outputFile.close();
	outputFile.setFileName(cppFileName);
	outputFile.open(QFile::WriteOnly);

	QString constructorBody;
	const QString constructorLineTemplate("\t\t{\"%1\", %2ull},\n");
	auto sortedTable = flip_map(parser.parsingResult().trigramOccurrenceTable);

	const quint64 thresholdTrigramCount = parser.parsingResult().totalTrigrammsCount / 2000; // Trigram with less than 0.05% occurrence rate are discarded
	quint64 actualTotalCount = 0;
	for (auto it = sortedTable.rbegin(); it != sortedTable.rend(); ++it)
	{
		if (it->first < thresholdTrigramCount)
			break;

		constructorBody.append(constructorLineTemplate.arg(it->second).arg(it->first));
		actualTotalCount += it->first;
	}

	stream << tableClassCppTemplate.arg(headerFileName).arg(languageName).arg(actualTotalCount).arg(constructorBody);
}
