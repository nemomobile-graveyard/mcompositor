include(../../meegotouch_config.pri)
TEMPLATE = lib
DEPENDPATH += .
INCLUDEPATH += .
CONFIG += dll release
QT += opengl network dbus
TARGET = decorator

publicHeaders.files = mabstractdecorator.h mrmiclient.h mrmiserver.h \
                      mabstractappinterface.h
HEADERS += $${publicHeaders.files} mrmiclient_p.h mrmiserver_p.h \
           mdecoratordbusadaptor.h mdecoratordbusinterface.h
SOURCES += mabstractdecorator.cpp mrmiclient.cpp mrmiserver.cpp \
           mabstractappinterface.cpp mdecoratordbusadaptor.cpp \
           mdecoratordbusinterface.cpp

publicHeaders.path = $$M_INSTALL_HEADERS/libdecorator
INSTALLS += publicHeaders
target.path=/usr/lib
INSTALLS += target
QMAKE_EXTRA_TARGETS += check
check.depends = $$TARGET
check.commands = $$system(true)
