TEMPLATE = app

TARGET = ut_restackwindows

DEPENDPATH += ../../../src
INCLUDEPATH += ../../../src

HEADERS += ut_restackwindows.cpp stats.h
SOURCES += ut_restackwindows.cpp
POST_TARGETDEPS += ../../../src/mrestacker.o

LIBS += -lX11 ../../../src/mrestacker.o
QT += testlib core

target.path = /usr/lib/mcompositor-unit-tests
INSTALLS += target
