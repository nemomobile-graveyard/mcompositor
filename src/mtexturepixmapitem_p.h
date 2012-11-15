/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
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

#ifndef DUITEXTUREPIXMAPITEM_P_H
#define DUITEXTUREPIXMAPITEM_P_H

#include <QObject>
#include <QRect>
#include <QRegion>
#include <QPointer>
#include <QtOpenGL>
#include <X11/Xlib.h>

#ifdef GLES2_VERSION
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <EGL/eglext.h>
class EglResourceManager;
class EglTextureManager;
#elif DESKTOP_VERSION
#include <GL/glx.h>
#include <GL/gl.h>
#endif

#include "mtexturefrompixmap.h"
#include "scenegraph/scenenode.h"

class QGLWidget;
class QGraphicsItem;
class MTexturePixmapItem;
class QTransform;

/*! Internal private implementation of MTexturePixmapItem
  Warning! Interface here may change at any time!
 */
class MTexturePixmapPrivate: public Item2DInterface
{
    Q_OBJECT
public:
    MTexturePixmapPrivate(Window window, MTexturePixmapItem *item);
    ~MTexturePixmapPrivate();
    void init();
    void updateWindowPixmap(XRectangle *rects = 0, int num = 0);
    void saveBackingStore();
    bool isDirectRendered() const;
    void resize(int w, int h);
    // \re-imp
    virtual QTransform transform() const;
    virtual bool isVisible();
    virtual bool hasAlpha();
    virtual qreal opacity();
    // \re-imp
                
    static QGLWidget *glwidget;
    Window window;
    MTextureFromPixmap TFP;
    bool direct_fb_render;

    QRect brect;
    QRegion damageRegion;
    QTimer damageRetryTimer;
    qreal angle;

    MTexturePixmapItem *item;

    // Contains a limited number of server times we received damage
    // notifications for this window.  Only used by the EGL variant
    // to throttle repairs if the window is transitioning.
    QList<Time> *pastDamages;
#ifdef WINDOW_DEBUG
    unsigned item_painted; // for unit testing
#endif

#ifdef GLES2_VERSION
    static EglTextureManager *texman;
    static EglResourceManager *eglresource;
#endif

 private slots:
    void updateShapeRegion();
};

#endif //DUITEXTUREPIXMAPITEM_P_H
