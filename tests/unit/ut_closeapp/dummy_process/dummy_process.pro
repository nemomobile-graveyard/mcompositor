TEMPLATE = app
TARGET = dummy_process
target.path = /usr/lib/mcompositor-unit-tests/
INSTALLS += target
DEPENDPATH += /usr/include/meegotouch/mcompositor
INCLUDEPATH += ../../../src

# Input
SOURCES += dummy_process.cpp

QT += core gui
CONFIG += debug
