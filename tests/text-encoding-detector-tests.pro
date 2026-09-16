TEMPLATE = app
TARGET = text-encoding-detector-tests

CONFIG += console testcase strict_c++ c++2b
CONFIG -= c++17 c++2a

CONFIG(release, debug|release) {
	CONFIG += optimize_full

	# global.pri's LTO flags: CONFIG += ltcg would contradict them with -fno-fat-lto-objects when global.pri is included
	win* {
		QMAKE_CXXFLAGS += /GL
		QMAKE_LFLAGS += /LTCG:INCREMENTAL
	}
	linux* {
		QMAKE_CXXFLAGS += -flto=auto -ffat-lto-objects
		QMAKE_CFLAGS   += -flto=auto -ffat-lto-objects
		QMAKE_LFLAGS   += -flto=auto
	}
	mac* {
		QMAKE_CXXFLAGS += -flto=thin
		QMAKE_CFLAGS   += -flto=thin
		QMAKE_LFLAGS   += -flto=thin
	}
}

QT = core core5compat

DEFINES += CATCH_CONFIG_ENABLE_BENCHMARKING

# The parent's global.pri where this repo is a submodule; a standalone build (CI) restates the flags from it that matter here
exists(../../global.pri) {
	include(../../global.pri)
} else {
	win*:QMAKE_CXXFLAGS += /utf-8
	win*:QMAKE_CXXFLAGS_WARN_ON = /W4
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
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX

	QMAKE_LFLAGS += /DEBUG
	# /OPT:REF and /OPT:ICF default to on only while /DEBUG is absent, so they must be restated alongside it.
	CONFIG(release, debug|release):QMAKE_LFLAGS += /OPT:REF /OPT:ICF
}

linux* | mac* | freebsd {
	QMAKE_CXXFLAGS_WARN_ON *= -Wall
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
