TARGET = text_encoding_detector
TEMPLATE = lib
CONFIG += staticlib

QT = core core5compat

CONFIG += strict_c++ c++2b

include(../../global.pri)

mac* | linux* | freebsd{
	CONFIG(release, debug|release):CONFIG *= Release optimize_full
	CONFIG(debug, debug|release):CONFIG *= Debug
}

Release:OUTPUT_DIR=release
Debug:OUTPUT_DIR=debug

DESTDIR  = ../../bin/$${OUTPUT_DIR}
OBJECTS_DIR = ../../build/$${OUTPUT_DIR}/$${TARGET}
MOC_DIR     = ../../build/$${OUTPUT_DIR}/$${TARGET}
UI_DIR      = ../../build/$${OUTPUT_DIR}/$${TARGET}
RCC_DIR     = ../../build/$${OUTPUT_DIR}/$${TARGET}

INCLUDEPATH += \
	../../cpputils \
	../../cpp-template-utils \
	../../cpp-template-utils/3rdparty

win*{
	QMAKE_CXXFLAGS += /MP /Zi
	Debug:QMAKE_CXXFLAGS += /JMC
	QMAKE_CXXFLAGS += /std:c++latest /permissive- /Zc:__cplusplus
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX

	Debug:QMAKE_LFLAGS += /INCREMENTAL
	Release:QMAKE_LFLAGS += /OPT:REF /OPT:ICF
}

linux*|mac*|freebsd{
	QMAKE_CXXFLAGS_WARN_ON += -pedantic-errors
	QMAKE_CFLAGS_WARN_ON += -pedantic-errors
	QMAKE_CXXFLAGS_WARN_ON *= -Wall

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}

win32*:*msvc* {
	QMAKE_CXXFLAGS += /FS
}

# Shared with ../tests, which compiles the same sources under its own flags instead of linking this library
include($$PWD/text-encoding-detector.pri)
