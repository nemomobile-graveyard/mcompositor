include(../../meegotouch_config.pri)
TEMPLATE = lib
DEPENDPATH += .
INCLUDEPATH += .
CONFIG += dll release
QT += opengl network dbus
TARGET = decorator

DBUS_ADAPTORS = mdecorator_dbus.xml
DBUS_INTERFACES = mdecorator_dbus.xml
QMAKE_QDBUSXML2CPP = $$[QT_INSTALL_BINS]/qdbusxml2cpp -i mabstractappinterface.h \$(if $(filter mdecorator_dbus_interface.%,\$@),-c MDecoratorInterface)

publicHeaders.files = mabstractdecorator.h mrmiclient.h mrmiserver.h \
		      mabstractappinterface.h mdecorator_dbus_interface.h
HEADERS += $${publicHeaders.files} mrmiclient_p.h mrmiserver_p.h
SOURCES += mabstractdecorator.cpp mrmiclient.cpp mrmiserver.cpp \
           mabstractappinterface.cpp
PRE_TARGETDEPS += mdecorator_dbus_interface.h

publicHeaders.path = $$M_INSTALL_HEADERS/libdecorator
publicHeaders.CONFIG += no_check_exist
INSTALLS += publicHeaders
target.path = /usr/lib
INSTALLS += target
QMAKE_EXTRA_TARGETS += check
check.depends = $$TARGET
check.commands = $$system(true)
