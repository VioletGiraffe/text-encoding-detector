TARGET = text_encoding_detector
TEMPLATE = lib
CONFIG += staticlib

CONFIG += c++11

QT = core

mac* | linux*{
    CONFIG(release, debug|release):CONFIG += Release
    CONFIG(debug, debug|release):CONFIG += Debug
}

Release:OUTPUT_DIR=release
Debug:OUTPUT_DIR=debug

DESTDIR  = ../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../build/$${OUTPUT_DIR}/text_encoding_detector
MOC_DIR     = ../../build/$${OUTPUT_DIR}/text_encoding_detector
UI_DIR      = ../../build/$${OUTPUT_DIR}/text_encoding_detector
RCC_DIR     = ../../build/$${OUTPUT_DIR}/text_encoding_detector

INCLUDEPATH += ../../qtutils ../../cpputils/

win*{
	QMAKE_CXXFLAGS += /MP
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX
	QMAKE_CXXFLAGS_WARN_ON = -W4

	QMAKE_LFLAGS += /INCREMENTAL /DEBUG:FASTLINK
}

linux*|mac*{
	QMAKE_CXXFLAGS += -pedantic-errors
	QMAKE_CFLAGS += -pedantic-errors
	QMAKE_CXXFLAGS_WARN_ON = -Wall -Wno-c++11-extensions -Wno-local-type-template-args -Wno-deprecated-register

	CONFIG(release, debug|release):CONFIG += Release
	CONFIG(debug, debug|release):CONFIG += Debug

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}

win32*:!*msvc2012:*msvc*:!*msvc2010:*msvc* {
	QMAKE_CXXFLAGS += /FS
}

HEADERS += \
	src/ctextparser.h \
	src/trigramfrequencytables/ctrigramfrequencytable_english.h \
	src/trigramfrequencytables/ctrigramfrequencytable_russian.h \
	src/ctextencodingdetector.h

SOURCES += \
	src/ctextparser.cpp \
	src/trigramfrequencytables/ctrigramfrequencytable_english.cpp \
	src/trigramfrequencytables/ctrigramfrequencytable_russian.cpp \
	src/ctextencodingdetector.cpp
