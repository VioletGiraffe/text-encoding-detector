DESTDIR  = bin
TARGET = text_analyzer
TEMPLATE = app
CONFIG += staticlib c++2b console

QT = core core5compat

OBJECTS_DIR = build
MOC_DIR     = build
UI_DIR      = build
RCC_DIR     = build

linux*|mac*|freebsd{
	QMAKE_CXXFLAGS += -pedantic-errors
	QMAKE_CFLAGS += -pedantic-errors
	QMAKE_CXXFLAGS_WARN_ON = -Wall -Wno-c++11-extensions -Wno-local-type-template-args -Wno-deprecated-register
}

win* {
	QMAKE_CXXFLAGS += /MP /Zi /wd4251 /JMC /FS
	QMAKE_CXXFLAGS += /std:c++latest /permissive- /Zc:__cplusplus
	QMAKE_LFLAGS += /DEBUG:FASTLINK

	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX
	QMAKE_CXXFLAGS_WARN_ON = -W4
}

INCLUDEPATH += \
	../text-encoding-detector/src/ \
	../../cpputils \
	../../cpp-template-utils \
	../../cpp-template-utils/3rdparty

SOURCES += src/main.cpp \
	../text-encoding-detector/src/ctextparser.cpp

HEADERS += \
	../text-encoding-detector/src/ctextparser.h
