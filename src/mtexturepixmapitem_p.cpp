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

#ifdef DESKTOP_VERSION
#define GL_GLEXT_PROTOTYPES 1
#endif
#include "mtexturepixmapitem.h"
#include "mcompositemanager.h"
#include "mdecoratorframe.h"
#include "msplashscreen.h"
#include "mrender.h"

#include <QX11Info>
#include <QRect>
#include <QMatrix4x4>

#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>
#ifdef GLES2_VERSION
#include <GLES2/gl2.h>
#elif DESKTOP_VERSION
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#endif

#include "mtexturepixmapitem_p.h"

QGLWidget *MTexturePixmapPrivate::glwidget = 0;

void MTexturePixmapPrivate::init()
{
    if (!item->isValid())
        return;
    
    resize(item->propertyCache()->realGeometry().width(),
           item->propertyCache()->realGeometry().height());
    item->setPos(item->propertyCache()->realGeometry().x(),
                 item->propertyCache()->realGeometry().y());
}

MTexturePixmapPrivate::MTexturePixmapPrivate(Qt::HANDLE window,
                                             MTexturePixmapItem *p)
    : window(window),
      TFP(),
      direct_fb_render(false), // root's children start redirected
      angle(0),
      item(p),
      pastDamages(0)
{
    if (!glwidget) {
        MCompositeManager *m = (MCompositeManager*)qApp;
        glwidget = m->glWidget();
    }
    if (item->propertyCache())
        item->propertyCache()->damageTracking(true);
    init();
}

MTexturePixmapPrivate::~MTexturePixmapPrivate()
{
    if (item->propertyCache())
        item->propertyCache()->damageTracking(false);

    if (TFP.drawable && !item->propertyCache()->isVirtual())
        XFreePixmap(QX11Info::display(), TFP.drawable);

    if (pastDamages)
        delete pastDamages;
}

void MTexturePixmapPrivate::saveBackingStore()
{
    if (item->propertyCache()->isVirtual()) {
        TFP.bind(item->windowPixmap());
        return;
    }
    if ((item->propertyCache()->is_valid && !item->propertyCache()->isMapped())
        || item->propertyCache()->isInputOnly()
        || !window)
        return;

    if (TFP.drawable)
        XFreePixmap(QX11Info::display(), TFP.drawable);

    // Pixmap is already freed. No sense to bind it to texture
    if (item->isClosing() || item->window() < QX11Info::appRootWindow())
        return;

    Drawable pixmap = XCompositeNameWindowPixmap(QX11Info::display(), item->window());
    TFP.bind(pixmap);
}

void MTexturePixmapPrivate::resize(int w, int h)
{
    if (!window)
        return;
    
    if (!brect.isEmpty() && !item->isDirectRendered() && (brect.width() != w || brect.height() != h)) {
        item->saveBackingStore();
        item->updateWindowPixmap();
    }
    brect.setWidth(w);
    brect.setHeight(h);
    MRender::setWindowGeometry(item, brect);
}

QTransform MTexturePixmapPrivate::transform() const
{
    return item->sceneTransform();
}

bool MTexturePixmapPrivate::isVisible()
{
     if (!item->propertyCache())  // this window is dead
         return false;
     
     MCompositeWindow *man = 0;
     if (item->propertyCache()->isDecorator() &&
         (!(man = MDecoratorFrame::instance()->managedClient()) ||
          man->isWindowTransitioning()))
         // if we have a transition animation, don't draw the decorator
         // lest we can have it drawn with the transition (especially
         // when desktop window is not yet shown, NB#192454)
         return false;
    
     if (item->isDirectRendered() || !item->isVisible()
         || !(item->propertyCache()->isMapped() || 
              item->isWindowTransitioning())
         || item->propertyCache()->isInputOnly())
         return false;
     
     return true;
}

bool MTexturePixmapPrivate::hasAlpha()
{
    return (item->propertyCache()->hasAlphaAndIsNotOpaque() 
            || item->opacity() < 1.0f);
}

qreal MTexturePixmapPrivate::opacity()
{
    if (item->opacity() < 1.0f) {
        setBlendFunc(static_cast<MCompositeManager*>(qApp)->splashed(item)
                     ? GL_ONE : GL_SRC_ALPHA,
                     GL_ONE_MINUS_SRC_ALPHA);
    }
    return item->opacity();
}

void MTexturePixmapItem::updateWindowPixmapProxy()
{
    updateWindowPixmap(0, 0, ((MCompositeManager*)qApp)->getServerTime());
}

bool MTexturePixmapItem::isDirectRendered() const
{
    return d->direct_fb_render;
}

void MTexturePixmapItem::paint(QPainter *painter,
                               const QStyleOptionGraphicsItem *option,
                               QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    Q_UNUSED(widget);
}

void MTexturePixmapItem::clearTexture()
{
    glBindTexture(GL_TEXTURE_2D, d->TFP.textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, 0);
    
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);
}

void MTexturePixmapItem::saveBackingStore()
{
    d->saveBackingStore();
}

void MTexturePixmapItem::resize(int w, int h)
{
    d->resize(w, h);
    this->MCompositeWindow::resize(w, h);
}

QSizeF MTexturePixmapItem::sizeHint(Qt::SizeHint, const QSizeF &) const
{
    return boundingRect().size();
}

QRectF MTexturePixmapItem::boundingRect() const
{
    return d->brect;
}

MTexturePixmapPrivate* MTexturePixmapItem::renderer() const
{
    return d;
}

GLuint MTexturePixmapItem::texture()
{
    return d->TFP.textureId;
}
