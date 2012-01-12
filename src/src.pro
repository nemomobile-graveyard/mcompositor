include(../meegotouch_config.pri)

contains(QT_CONFIG, opengles2) {
     message("building Makefile for EGL/GLES2 version")
     DEFINES += GLES2_VERSION
     SOURCES += mtexturepixmapitem_egl.cpp mcompositewindowgroup.cpp mtexturefrompixmap_egl.cpp
     HEADERS += mcompositewindowgroup.h
     publicHeaders.files +=  mcompositewindowgroup.h
} else {
     exists($$QMAKE_INCDIR_OPENGL/EGL) {
         # Qt was not built with EGL/GLES2 support but if EGL is present
         # ensure we still use the EGL back-end.
         message("building Makefile for EGL/GLES2 version")
         DEFINES += GLES2_VERSION
         SOURCES += mtexturepixmapitem_egl.cpp mtexturefrompixmap_egl.cpp
         LIBS += -lEGL
     } else {
         # Otherwise use GLX backend.
         message("building Makefile for GLX version")
         DEFINES += DESKTOP_VERSION GL_GLEXT_PROTOTYPES
         SOURCES += \
	 	    mtexturepixmapitem_glx.cpp \
	 	    mtexturefrompixmap_glx.cpp
     }
} 

contains(QT_CONFIG, xsync) {
    message("adding support for synced swapbuffers")
    DEFINES += HAVE_XSYNC
}
contains(DEFINES, WINDOW_DEBUG) {
    DEFINES += DEBUG_SCENEGRAPH
}

TEMPLATE = lib
TARGET = mcompositor
DEPENDPATH += .
QT += dbus

# Input
INCLUDEPATH += ../decorators/libdecorator ./scenegraph
HEADERS += \
    mtexturepixmapitem.h \
    mtexturefrompixmap.h \
    mtexturepixmapitem_p.h \
    mcompositewindow.h \
    mwindowpropertycache.h \
    mcompositemanager.h \
    mcompositemanager_p.h \
    mdevicestate.h \
    mcompatoms_p.h \
    mdecoratorframe.h \
    msplashscreen.h \
    mcompositemanagerextension.h \
    mcompositewindowshadereffect.h \
    mcompmgrextensionfactory.h \
    mcontextproviderwrapper.h \
    mcontextproviderwrapper_p.h \
    mcompositewindowanimation.h \
    mdynamicanimation.h \
    mrestacker.h \
    mstatusbartexture.h \
    mrender.h

SOURCES += \
    mtexturepixmapitem_p.cpp \
    mcompositewindow.cpp \
    mwindowpropertycache.cpp \
    mcompositemanager.cpp \
    mdevicestate.cpp \
    mdecoratorframe.cpp \
    msplashscreen.cpp \
    mcompositemanagerextension.cpp \
    mcompositewindowshadereffect.cpp \
    mcontextproviderwrapper.cpp \
    mcompositewindowanimation.cpp \
    mdynamicanimation.cpp \
    mrestacker.cpp \
    mstatusbartexture.cpp \
    mrender.cpp \
    scenegraph/scenenode.cpp \
    scenegraph/scenerender.cpp \
    scenegraph/texturecoords.cpp 

CONFIG += release link_pkgconfig
PKGCONFIG += contextsubscriber-1.0 contextprovider-1.0
QT += core gui opengl

# TODO: refactor the headers to exclude private stuff
publicHeaders.files += mcompositewindow.h \
                      mtexturefrompixmap.h \
                      mcompositemanager.h \
                      mcompositewindowshadereffect.h \
                      mcompositemanagerextension.h \
                      mwindowpropertycache.h \
                      mcompatoms_p.h \
                      mcompmgrextensionfactory.h \
                      mcompositewindowanimation.h \
                      mstatusbartexture.h \
                      mdevicestate.h \
                      scenegraph/texturecoords.h
publicHeaders.path = $$M_INSTALL_HEADERS/mcompositor
INSTALLS += publicHeaders

target.path += /usr/lib
INSTALLS += target 

contextkitXml.files = org.maemo.mcompositor.context
contextkitXml.path = /usr/share/contextkit/providers
INSTALLS += contextkitXml

LIBS += -lXdamage -lXcomposite -lXfixes -lX11-xcb -lxcb-render -lxcb-shape \
        -lXrandr ../decorators/libdecorator/libdecorator.so

QMAKE_EXTRA_TARGETS += check
QMAKE_CLEAN += scenegraph/*~
check.depends = $$TARGET
check.commands = $$system(true)
