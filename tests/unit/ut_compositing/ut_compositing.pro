include(../../../meegotouch_config.pri)
TEMPLATE = app
TARGET = ut_compositing
target.path = /usr/lib/mcompositor-unit-tests/
INSTALLS += target
DEPENDPATH += /usr/include/meegotouch/mcompositor
INCLUDEPATH += ../../../src

DEFINES += TESTS

contains(QT_CONFIG, opengles2) {
     message("building Makefile for EGL/GLES2 version")
     DEFINES += GLES2_VERSION
} else {
     exists($$QMAKE_INCDIR_OPENGL/EGL) {
         message("building Makefile for EGL/GLES2 version")
         DEFINES += GLES2_VERSION
     } else {
         message("building Makefile for GLX version")
         DEFINES += DESKTOP_VERSION
     }
}

LIBS += ../../../decorators/libdecorator/libdecorator.so ../../../src/libmcompositor.so

# Input
HEADERS += ut_compositing.h
SOURCES += ut_compositing.cpp

QT += testlib core gui opengl dbus
CONFIG += debug

CONFIG += link_pkgconfig
PKGCONFIG += contextsubscriber-1.0 contextprovider-1.0
