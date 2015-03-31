	DESTDIR  = ../../bin
TARGET = text_encoding_detector
TEMPLATE = lib
CONFIG += staticlib

QT = core

OBJECTS_DIR = ../../build/text_encoding_detector
MOC_DIR     = ../../build/text_encoding_detector
UI_DIR      = ../../build/text_encoding_detector
RCC_DIR     = ../../build/text_encoding_detector

win*{
	QMAKE_CXXFLAGS += /MP
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX
	QMAKE_CXXFLAGS_WARN_ON = -W4
}

linux*|mac*{
	QMAKE_CXXFLAGS += -pedantic-errors -std=c++1y
	QMAKE_CFLAGS += -pedantic-errors
	QMAKE_CXXFLAGS_WARN_ON = -Wall -Wno-c++11-extensions -Wno-local-type-template-args -Wno-deprecated-register
}

mac*{
	CONFIG += c++11
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
