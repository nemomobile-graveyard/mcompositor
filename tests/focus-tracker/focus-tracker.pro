TARGET = focus-tracker
target.path = /usr/bin
INSTALLS += target
QMAKE_CFLAGS += -Wno-unused-parameter
LIBS += -lX11
QT -= gui core
SOURCES += focus-tracker.c
