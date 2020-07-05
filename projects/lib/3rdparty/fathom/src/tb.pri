DEFINES += Z_PREFIX
CONFIG(release, debug|release):DEFINES += NDEBUG

!win32-msvc* {
    QMAKE_CFLAGS += -std=c99
}
win32-msvc* {
    QMAKE_CFLAGS += -TP -D_CRT_SECURE_NO_DEPRECATE
}
INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD
SOURCES += $$PWD/tbprobe.c \
	$$PWD/tbchess.c
HEADERS += $$PWD/tbprobe.h \
	$$PWD/tbconfig.h
