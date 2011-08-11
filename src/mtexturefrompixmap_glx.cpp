/***************************************************************************
**
** Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (directui@nokia.com)
**
** This file is part of mcompositor.
**
** If you have questions regarding the use of this file, please contact
** Nokia at directui@nokia.com.
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation
** and appearing in the file LICENSE.LGPL included in the packaging
** of this file.
**
****************************************************************************/

#define GLX_GLXEXT_PROTOTYPES 1
#define GLX_EXT_texture_from_pixmap 1

#include "mtexturefrompixmap.h"

#include <QDebug>
#include <QX11Info>

#include <GL/glx.h>
#include <GL/glxext.h>

#if defined(GLX_VERSION_1_3)
#ifndef GLX_TEXTURE_2D_BIT_EXT
#define GLX_TEXTURE_2D_BIT_EXT             0x00000002
#define GLX_TEXTURE_RECTANGLE_BIT_EXT      0x00000004
#define GLX_BIND_TO_TEXTURE_RGB_EXT        0x20D0
#define GLX_BIND_TO_TEXTURE_RGBA_EXT       0x20D1
#define GLX_BIND_TO_MIPMAP_TEXTURE_EXT     0x20D2
#define GLX_BIND_TO_TEXTURE_TARGETS_EXT    0x20D3
#define GLX_Y_INVERTED_EXT                 0x20D4
#define GLX_TEXTURE_FORMAT_EXT             0x20D5
#define GLX_TEXTURE_TARGET_EXT             0x20D6
#define GLX_MIPMAP_TEXTURE_EXT             0x20D7
#define GLX_TEXTURE_FORMAT_NONE_EXT        0x20D8
#define GLX_TEXTURE_FORMAT_RGB_EXT         0x20D9
#define GLX_TEXTURE_FORMAT_RGBA_EXT        0x20DA
#define GLX_TEXTURE_2D_EXT                 0x20DC
#define GLX_TEXTURE_RECTANGLE_EXT          0x20DD
#define GLX_FRONT_LEFT_EXT                 0x20DE
#endif

typedef void (*_glx_bind)(Display *, GLXDrawable, int , const int *);
typedef void (*_glx_release)(Display *, GLXDrawable, int);
static  _glx_bind glXBindTexImageEXT = 0;
static  _glx_release glXReleaseTexImageEXT = 0;
static GLXFBConfig config = 0;
static GLXFBConfig configAlpha = 0;
static bool configInverted = false, configAlphaInverted = false;

static void getConfig(bool alpha)
{
    Display *display = QX11Info::display();
    int screen = QX11Info::appScreen();

    int pixmap_config[] = {
        alpha ? GLX_BIND_TO_TEXTURE_RGBA_EXT :
                             GLX_BIND_TO_TEXTURE_RGB_EXT, True,
        GLX_DRAWABLE_TYPE, GLX_PIXMAP_BIT,
        GLX_BIND_TO_TEXTURE_TARGETS_EXT, GLX_TEXTURE_2D_BIT_EXT,
        GLX_DOUBLEBUFFER, True,
        GLX_Y_INVERTED_EXT, GLX_DONT_CARE,
        None
    };

    int c = 0;
    GLXFBConfig *configs = 0;
    configs = glXChooseFBConfig(display, screen, pixmap_config, &c);
    if (!configs) {
        qWarning() << "No appropriate" <<
	      (alpha ? "alpha" : "non-alpha") << "GLXFBconfig found!"
              " Falling back to custom TFP.";
        return;
    }

    int inverted;
    glXGetFBConfigAttrib(display, configs[0], GLX_Y_INVERTED_EXT, &inverted);
    if (alpha) {
        configAlpha = configs[0];
        configAlphaInverted = inverted ? true : false;
    } else {
        config = configs[0];
        configInverted = inverted ? true : false;
    }
    XFree(configs);
}

static bool hasTextureFromPixmap()
{
    static bool checked = false, hasTfp = false;

    if (!checked) {
	checked = true;
        QList<QByteArray> exts = QByteArray(glXQueryExtensionsString(QX11Info::display(), QX11Info::appScreen())).split(' ');
        if (exts.contains("GLX_EXT_texture_from_pixmap")) {
            glXBindTexImageEXT = (_glx_bind) glXGetProcAddress((const GLubyte *)"glXBindTexImageEXT");
            glXReleaseTexImageEXT = (_glx_release) glXGetProcAddress((const GLubyte *)"glXReleaseTexImageEXT");
        }
	getConfig(false);
	getConfig(true);
        if (glXBindTexImageEXT && glXReleaseTexImageEXT
	    && config && configAlpha)
	    hasTfp = true;
    }
    return hasTfp;
}
#endif

struct MTextureFromPixmapPrivate {
    GLXPixmap glpixmap;

    MTextureFromPixmapPrivate() : glpixmap(0) {}
    ~MTextureFromPixmapPrivate()
    {
        freeGLPixmap();
    }

    void freeGLPixmap()
    {
        if (glpixmap) {
            Display *display = QX11Info::display();
            glXReleaseTexImageEXT(display, glpixmap, GLX_FRONT_LEFT_EXT);
            glXDestroyPixmap(display, glpixmap);
            glpixmap = 0;
        }
    }
};

MTextureFromPixmap::MTextureFromPixmap()
    : d(new MTextureFromPixmapPrivate)
{
}

MTextureFromPixmap::~MTextureFromPixmap()
{
    delete d;
}

bool MTextureFromPixmap::invertedTexture() const
{
    if (hasTextureFromPixmap())
        return alpha ? configAlphaInverted : configInverted;
    else
        return false;
}

void MTextureFromPixmap::update()
{
    if (hasTextureFromPixmap() || !drawable)
        return;

    QPixmap qp = QPixmap::fromX11Pixmap(drawable);

    QT_TRY {
        QImage img = QGLWidget::convertToGLFormat(qp.toImage());
        glBindTexture(GL_TEXTURE_2D, textureId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
    } QT_CATCH(std::bad_alloc e) {
        /* XGetImage() failed, the window has been unmapped. */;
        qWarning("MTexturePixmapItem::%s(): std::bad_alloc e", __func__);
    }
}

void MTextureFromPixmap::bind(Drawable draw)
{
    drawable = draw;

    if (!hasTextureFromPixmap()) {
        update();
        return;
    }

    d->freeGLPixmap();

    if (!drawable)
        return;

    const int pixmapAttribs[] = {
        GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
        GLX_TEXTURE_FORMAT_EXT, alpha ?
        GLX_TEXTURE_FORMAT_RGBA_EXT : GLX_TEXTURE_FORMAT_RGB_EXT,
        None
    };
    Display *display = QX11Info::display();

    d->glpixmap = glXCreatePixmap(display, alpha ?
                                  configAlpha : config,
                                  drawable, pixmapAttribs);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glXBindTexImageEXT(display, d->glpixmap, GLX_FRONT_LEFT_EXT, NULL);
}

void MTextureFromPixmap::unbind()
{
    d->freeGLPixmap();
    drawable = 0;
}
