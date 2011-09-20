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

#include "mtexturepixmapitem.h"
#include "mtexturepixmapitem_p.h"

#include <QPainterPath>
#include <QRect>
#include <QGLContext>
#include <QX11Info>
#include <vector>

#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>

#include <GL/gl.h>
#include <GL/glu.h>

void MTexturePixmapItem::init()
{
    MWindowPropertyCache *pc = propertyCache();
    if (isValid() && !pc->isMapped()) {
        qWarning("MTexturePixmapItem::%s(): Failed getting offscreen pixmap",
                 __func__);
        return;
    } else if (!isValid())
        return;

    glGenTextures(1, &d->TFP.textureId);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, d->TFP.textureId);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    d->TFP.alpha = pc->hasAlpha();

    d->inverted_texture = d->TFP.invertedTexture();

    saveBackingStore();
}

MTexturePixmapItem::MTexturePixmapItem(Window window,
                                       MWindowPropertyCache *pc,
                                       QGraphicsItem *parent)
    : MCompositeWindow(window, pc, parent),
      d(new MTexturePixmapPrivate(window, this))
{
    init();
}

void MTexturePixmapItem::enableDirectFbRendering()
{
    if (d->item->propertyCache())
        d->item->propertyCache()->damageTracking(false);

    if (d->direct_fb_render)
        return;

    d->direct_fb_render = true;
    // Just free the off-screen surface but re-use the
    // existing texture id, so don't delete it yet.

    Drawable pixmap = d->TFP.drawable;
    d->TFP.unbind();

    if (pixmap)
        XFreePixmap(QX11Info::display(), pixmap);

    XCompositeUnredirectWindow(QX11Info::display(), window(),
                               CompositeRedirectManual);
    XSync(QX11Info::display(), FALSE);
}

void MTexturePixmapItem::enableRedirectedRendering()
{
    if (d->item->propertyCache())
        d->item->propertyCache()->damageTracking(true);

    if (!d->direct_fb_render)
        return;

    d->direct_fb_render = false;
    XCompositeRedirectWindow(QX11Info::display(), window(),
                             CompositeRedirectManual);
    XSync(QX11Info::display(), FALSE);
    saveBackingStore();
    updateWindowPixmap();
}

MTexturePixmapItem::~MTexturePixmapItem()
{
    cleanup();
    delete d;
}

void MTexturePixmapItem::cleanup()
{
    GLuint textureId = d->TFP.textureId;
    Drawable pixmap = d->TFP.drawable;

    d->TFP.unbind();
    glDeleteTextures(1, &textureId);

    if (pixmap)
        XFreePixmap(QX11Info::display(), pixmap);
}

void MTexturePixmapItem::updateWindowPixmap(XRectangle *rects, int num,
                                            Time when)
{
    Q_UNUSED(rects);
    Q_UNUSED(num);
    Q_UNUSED(when);

    if (isWindowTransitioning() || d->direct_fb_render
        || propertyCache()->isInputOnly())
        return;

    propertyCache()->damageSubtract();
    d->TFP.update();
    update();
}
