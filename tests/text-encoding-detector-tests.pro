TEMPLATE = app
TARGET = text-encoding-detector-tests

CONFIG += console testcase strict_c++ c++2b
CONFIG -= c++17 c++2a

CONFIG(release, debug|release):CONFIG += optimize_full ltcg

QT = core
greaterThan(QT_MAJOR_VERSION, 5) {
	QT += core5compat
}

DEFINES += CATCH_CONFIG_ENABLE_BENCHMARKING

include(../../global.pri)

CONFIG(debug, debug|release) {
	OUTPUT_DIR=debug
	DEFINES += _DEBUG
} else {
	OUTPUT_DIR=release
	DEFINES += NDEBUG=1
}

DESTDIR = $$PWD/bin/$${OUTPUT_DIR}
OBJECTS_DIR = $$PWD/build/$${OUTPUT_DIR}

win* {
	QMAKE_CXXFLAGS += /MP /Zi /FS /std:c++latest /permissive- /Zc:__cplusplus
	QMAKE_CXXFLAGS_WARN_ON = /W4

	# /OPT:REF and /OPT:ICF default to on only while /DEBUG is absent, so they must be restated alongside it.
	# FULL rather than FASTLINK: a fastlink PDB is unusable to external profilers.
	CONFIG(release, debug|release):QMAKE_LFLAGS += /DEBUG:FULL /OPT:REF /OPT:ICF
}

linux* | mac* | freebsd {
	QMAKE_CXXFLAGS_WARN_ON = -Wall
}

INCLUDEPATH += \
	$$PWD/../text-encoding-detector/src \
	$$PWD/../../cpputils \
	$$PWD/../../qtutils \
	$$PWD/../../cpp-template-utils \
	$$PWD/../../cpp-template-utils/3rdparty

# The benchmarks need a text of several MB, which the repository does not carry. This points at the analyzer's
# corpus folder; TEXT_ENCODING_DETECTOR_CORPUS and TEXT_ENCODING_DETECTOR_CORPUS_CODEC override it at run time,
# and the cases that need it report themselves skipped where it is absent.
DEFINES += DEFAULT_CORPUS_FILE=\\\"$$PWD/../text-encoding-detector/src/trigramfrequencytables/1/BEZOP.TXT\\\"

HEADERS += \
	benchmark_corpus.h

SOURCES += \
	main.cpp \
	benchmark_corpus.cpp \
	ctextencodingdetector_tests.cpp \
	ctextencodingdetector_benchmarks.cpp \
	trigram_container_benchmarks.cpp

include(../text-encoding-detector/text-encoding-detector.pri)
include(../../cpputils/assert/assert.pri)
