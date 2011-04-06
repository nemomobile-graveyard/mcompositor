include(../../../meegotouch_config.pri)
TEMPLATE = app
TARGET = ut_stacking
target.path = /usr/lib/mcompositor-unit-tests/
INSTALLS += target
DEPENDPATH += /usr/include/meegotouch/mcompositor
INCLUDEPATH += ../../../src

DEFINES += TESTS

LIBS += ../../../decorators/libdecorator/libdecorator.so ../../../src/libmcompositor.so

# Input
HEADERS += ut_stacking.h
SOURCES += ut_stacking.cpp

QT += testlib core gui opengl dbus
CONFIG += debug
