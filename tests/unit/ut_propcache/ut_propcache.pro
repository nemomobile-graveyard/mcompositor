include(../../../meegotouch_config.pri)
TEMPLATE = app
TARGET = ut_propcache
target.path = /usr/lib/mcompositor-unit-tests/
INSTALLS += target
DEPENDPATH += /usr/include/meegotouch/mcompositor
INCLUDEPATH += ../../../src

DEFINES += TESTS

LIBS += ../../../decorators/libdecorator/libdecorator.so \
        ../../../src/libmcompositor.so -lX11

# Input
HEADERS += ut_propcache.h
SOURCES += ut_propcache.cpp

QT += testlib core gui opengl dbus
CONFIG += debug link_pkgconfig
PKGCONFIG += x11

