DESTDIR  = bin
TARGET = text_analyzer
TEMPLATE = app
CONFIG += staticlib c++11 console

QT = core

OBJECTS_DIR = build
MOC_DIR     = build
UI_DIR      = build
RCC_DIR     = build

win*{
	QMAKE_CXXFLAGS += /MP
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX
	QMAKE_CXXFLAGS_WARN_ON = -W4
}

linux*|mac*|freebsd{
	QMAKE_CXXFLAGS += -pedantic-errors
	QMAKE_CFLAGS += -pedantic-errors
	QMAKE_CXXFLAGS_WARN_ON = -Wall -Wno-c++11-extensions -Wno-local-type-template-args -Wno-deprecated-register
}

win32*:!*msvc2012:*msvc*:!*msvc2010:*msvc* {
	QMAKE_CXXFLAGS += /FS
}

INCLUDEPATH += \
	../text-encoding-detector/src/ \
	../../cpputils

LIBS += -L../../bin -ltext_encoding_detector

SOURCES += src/main.cpp
