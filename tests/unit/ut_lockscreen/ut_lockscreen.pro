include(../../../meegotouch_config.pri)

contains(QT_CONFIG, opengles2) {
     message("building Makefile for EGL/GLES2 version")
     DEFINES += GLES2_VERSION
} else {
     exists($$QMAKE_INCDIR_OPENGL/EGL) {
         # Qt was not built with EGL/GLES2 support but if EGL is present
         # ensure we still use the EGL back-end.
         message("building Makefile for EGL/GLES2 version")
         DEFINES += GLES2_VERSION
     } else {
         # Otherwise use GLX backend.
         message("building Makefile for GLX version")
         DEFINES += DESKTOP_VERSION
     }
} 

TEMPLATE = app
TARGET = ut_lockscreen
target.path = /usr/lib/mcompositor-unit-tests/
INSTALLS += target
DEPENDPATH += /usr/include/meegotouch/mcompositor
INCLUDEPATH += ../../../src

DEFINES += TESTS

LIBS += ../../../decorators/libdecorator/libdecorator.so ../../../src/libmcompositor.so

# Input
HEADERS += ut_lockscreen.h
SOURCES += ut_lockscreen.cpp

QT += testlib core gui opengl dbus
CONFIG += debug

CONFIG += link_pkgconfig
PKGCONFIG += contextsubscriber-1.0 contextprovider-1.0
