include(../../meegotouch_config.pri)
TEMPLATE = lib
DEPENDPATH += .
INCLUDEPATH += .
CONFIG += dll release
QT += opengl network dbus
TARGET = decorator

HEADERS += mabstractdecorator.h \
           mrmiclient_p.h \
           mrmiclient.h \
           mrmiserver_p.h \
           mrmiserver.h \
           mabstractappinterface.h \
           mdecoratordbusadaptor.h \
           mdecoratordbusinterface.h
SOURCES += mabstractdecorator.cpp \
           mrmiclient.cpp \
           mrmiserver.cpp \
           mabstractappinterface.cpp \
           mdecoratordbusadaptor.cpp \
           mdecoratordbusinterface.cpp

target.path=/usr/lib
INSTALLS += target
QMAKE_EXTRA_TARGETS += check
check.depends = $$TARGET
check.commands = $$system(true)
