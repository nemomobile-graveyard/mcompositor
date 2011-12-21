TEMPLATE = app
TARGET = windowstack

target.path=/usr/bin

DEPENDPATH += .
INCLUDEPATH += . 

LIBS += -lXcomposite -lX11
SOURCES += windowstack.cpp 

INSTALLS += target
