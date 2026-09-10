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

# The parent's global.pri where this repo is a submodule; a standalone build (CI) restates the one flag from it that matters here
exists(../../global.pri) {
	include(../../global.pri)
} else {
	win*:QMAKE_CXXFLAGS += /utf-8
}

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
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX

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
	$$PWD/../../cpp-template-utils \
	$$PWD/../../cpp-template-utils/3rdparty

# The committed corpus the tests, benchmarks and study all read; see ../corpus/README.md
DEFINES += CORPUS_DIR=\\\"$$PWD/../corpus\\\"

HEADERS += \
	benchmark_corpus.h \
	mixed_content_scenarios.h

SOURCES += \
	main.cpp \
	benchmark_corpus.cpp \
	ctextencodingdetector_tests.cpp \
	ctextencodingdetector_benchmarks.cpp \
	detection_window_study.cpp \
	mixed_content_scenarios.cpp \
	trigram_container_benchmarks.cpp

include(../text-encoding-detector/text-encoding-detector.pri)
include(../../cpputils/assert/assert.pri)
