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

#include "mtexturefrompixmap.h"
#include "mtexturepixmapitem_p.h"

#include <QGLContext>
#include <QX11Info>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

static PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = 0; 
static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = 0; 
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = 0;
static EGLint attribs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE }; 

class EglResourceManager
{
public:
    EglResourceManager()
        : has_tfp(false) {
        if (!dpy)
            dpy = eglGetDisplay(EGLNativeDisplayType(QX11Info::display()));

        QString exts = QLatin1String(eglQueryString(dpy, EGL_EXTENSIONS));
        if ((exts.contains("EGL_KHR_image") &&
             exts.contains("EGL_KHR_image_pixmap") &&
             exts.contains("EGL_KHR_gl_texture_2D_image"))) {
            has_tfp = true;
            eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
            eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR"); 
            glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress("glEGLImageTargetTexture2DOES"); 
        } else {
            qDebug("No EGL tfp support.\n");
        }
    }

    static bool texturePixmapSupport() {
        if (!MTexturePixmapPrivate::eglresource)
            MTexturePixmapPrivate::eglresource = new EglResourceManager;
        return MTexturePixmapPrivate::eglresource->has_tfp;
    }

    static EGLConfig config;
    static EGLConfig configAlpha;
    static EGLDisplay dpy;

    bool has_tfp;
};

EGLConfig EglResourceManager::config = 0;
EGLConfig EglResourceManager::configAlpha = 0;
EGLDisplay EglResourceManager::dpy = 0;

EglResourceManager *MTexturePixmapPrivate::eglresource = 0;

MTextureFromPixmap::MTextureFromPixmap()
    : drawable(0),
      textureId(0),
      d(0),
      valid(false)
{
}

MTextureFromPixmap::~MTextureFromPixmap()
{
}

bool MTextureFromPixmap::invertedTexture() const
{
    if (EglResourceManager::texturePixmapSupport())
        return true;
    return false;
}

void MTextureFromPixmap::update()
{
    if (EglResourceManager::texturePixmapSupport())
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
    valid = false;

    if (!EglResourceManager::texturePixmapSupport()) {
        update();
        return;
    }

    unbind();

    if (!drawable)
        return;

    EGLImageKHR egl_image;
    egl_image = eglCreateImageKHR(MTexturePixmapPrivate::eglresource->dpy,
                                  0, EGL_NATIVE_PIXMAP_KHR,
                                  (EGLClientBuffer)drawable,
                                  attribs);

    if (egl_image == EGL_NO_IMAGE_KHR) {
        // window is probably unmapped
        /*qWarning("MTexturePixmapItem::%s(): Cannot create EGL image: 0x%x",
                 __func__, eglGetError());*/
        return;
    }

    glBindTexture(GL_TEXTURE_2D, textureId);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_image);
    eglDestroyImageKHR(MTexturePixmapPrivate::eglresource->dpy, egl_image);
    valid = true;
}

void MTextureFromPixmap::unbind()
{
    /* Free EGLImage from the texture */
    glBindTexture(GL_TEXTURE_2D, textureId);
    /*
     * Texture size 64x64 is minimum required by GL. But we can assume 0x0
     * works with modern drivers/hw.
     */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);

}

bool MTextureFromPixmap::isValid() const
{
    return valid;
}
