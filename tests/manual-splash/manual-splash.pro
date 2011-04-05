TEMPLATE = app
TARGET = manual-splash
QMAKE_CXXFLAGS+= -Wall
QMAKE_CFLAGS+= -Wall

CONFIG += debug

LIBS += -lX11

SOURCES += main.cpp

QT -= core gui

include(../../meegotouch_config.pri)
target.path += $$M_INSTALL_BIN
INSTALLS += target
