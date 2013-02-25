include(../../meegotouch_config.pri)

TEMPLATE = app
DEPENDPATH += ../libdecorator
INCLUDEPATH += ../libdecorator
CONFIG += release
QT += opengl dbus declarative

LIBS += ../libdecorator/libdecorator.so -lX11 -lXfixes

SOURCES += main.cpp \
    mdecoratorwindow.cpp \
    mdecorator.cpp \
    mdecoratorappinterface.cpp
HEADERS += \
    mdecoratorwindow.h \
    mdecorator.h \
    mdecoratorappinterface.h

RESOURCES += \
    res.qrc

OTHER_FILES += \
    qml/main.qml

QMAKE_EXTRA_TARGETS += check
check.depends = $$TARGET
check.commands = $$system(true)
target.path = /usr/bin
INSTALLS += target
