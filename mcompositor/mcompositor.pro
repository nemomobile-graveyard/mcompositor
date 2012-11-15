include(../meegotouch_config.pri)

TEMPLATE = app
INCLUDEPATH += ../src

LIBS += ../src/libmcompositor.so ../decorators/libdecorator/libdecorator.so

CONFIG += link_pkgconfig
PKGCONFIG += libsystemd-daemon

target.path += $$M_INSTALL_BIN
INSTALLS += target 

# Input
SOURCES += main.cpp

contains(DEFINES, WINDOW_DEBUG_ALOT) {
    HEADERS += xserverpinger.h
    SOURCES += xserverpinger.cpp
}

QT = core gui opengl
