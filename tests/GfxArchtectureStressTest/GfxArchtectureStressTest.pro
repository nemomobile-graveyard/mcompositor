TARGET = GfxArchtectureStressTest
CONFIG -= qt
INCLUDEPATH += $$QMAKE_INCDIR_OPENGL
for(p, QMAKE_LIBDIR_OPENGL_ES2):exists($$p):LIBS += -L$$p
LIBS += $$QMAKE_LIBS_EGL $$QMAKE_LIBS_OPENGL_ES2 $$QMAKE_LIBS_X11 -lXrender
SOURCES += main.cpp \
    application.cpp \
    util.cpp
HEADERS += application.h \
    util.h


