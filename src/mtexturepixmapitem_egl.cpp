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
#include "mcompositewindowgroup.h"
#include "mcompositewindowanimation.h"
#include "mcompositemanager.h"

#include <QPainterPath>
#include <QRect>
#include <QGLContext>
#include <QX11Info>
#include <QVector>

#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xfixes.h>

//#define GL_GLEXT_PROTOTYPES

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

class EglTextureManager
{
public:

    EglTextureManager() {
        genTextures(20);
    }

    ~EglTextureManager() {
        int sz = all_tex.size();
        glDeleteTextures(sz, all_tex.constData());
    }

    GLuint getTexture() {
        if (free_tex.empty())
            genTextures(10);
        GLuint ret = free_tex.back();
        free_tex.pop_back();
        return ret;
    }

    void closeTexture(GLuint texture) {
        // clear this texture
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, 0);
        free_tex.push_back(texture);
    }
private:

    void genTextures(int n) {
        GLuint tex[n + 1];
        glGenTextures(n, tex);
        for (int i = 0; i < n; i++) {
            free_tex.push_back(tex[i]);
            all_tex.push_back(tex[i]);
        }
    }

    QVector<GLuint> free_tex, all_tex;
};

EglTextureManager *MTexturePixmapPrivate::texman = 0;

void MTexturePixmapItem::init()
{
    if ((!isValid() && !propertyCache()->isVirtual())
        || propertyCache()->isInputOnly())
        return;
    
    if (!d->texman)
        d->texman = new EglTextureManager;
    
    d->TFP.textureId = d->texman->getTexture();
    glEnable(GL_TEXTURE_2D);
    
    glBindTexture(GL_TEXTURE_2D, d->TFP.textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    d->inverted_texture = d->TFP.invertedTexture();
    d->saveBackingStore();
    if (propertyCache()->isVirtual())
        // splash screen hasn't loaded the pixmap yet
        d->direct_fb_render = false;
    else
        d->direct_fb_render = (d->TFP.drawable == None);

    d->damageRetryTimer.setSingleShot(true);
    connect(&d->damageRetryTimer, SIGNAL(timeout()),
            SLOT(updateWindowPixmapProxy()));
}

MTexturePixmapItem::MTexturePixmapItem(Window window, MWindowPropertyCache *mpc,
                                       QGraphicsItem* parent)
    : MCompositeWindow(window, mpc, parent),
      d(new MTexturePixmapPrivate(window, this))
{
    init();
}

void MTexturePixmapItem::enableDirectFbRendering()
{
    if (d->direct_fb_render || propertyCache()->isVirtual())
        return;
    if (propertyCache())
        propertyCache()->damageTracking(false);

    d->direct_fb_render = true;

    Drawable pixmap = d->TFP.drawable;
    d->TFP.unbind();
    if (pixmap)
        XFreePixmap(QX11Info::display(), pixmap);

    XCompositeUnredirectWindow(QX11Info::display(), window(),
                               CompositeRedirectManual);
}

void MTexturePixmapItem::enableRedirectedRendering()
{
    if (!d->direct_fb_render || propertyCache()->isVirtual())
        return;
    if (propertyCache())
        propertyCache()->damageTracking(true);

    d->direct_fb_render = false;
    XCompositeRedirectWindow(QX11Info::display(), window(),
                             CompositeRedirectManual);
    saveBackingStore();
    updateWindowPixmap();
}

MTexturePixmapItem::~MTexturePixmapItem()
{
    cleanup();
    delete d; // frees the pixmap too
}

void MTexturePixmapItem::cleanup()
{
    d->TFP.unbind();
    if (d->TFP.textureId)
        d->texman->closeTexture(d->TFP.textureId);
}

void MTexturePixmapItem::updateWindowPixmap(XRectangle *rects, int num,
                                            Time when)
{
    // When a window is in transitioning limit the number of updates
    // to @limit/@expiry miliseconds.
    const unsigned expiry = 1000;
    const int      limit  =   30;

    if (hasTransitioningWindow()) {

        if (!windowAnimator()->isManuallyUpdated() &&
            MCompositeWindowAnimation::hasActiveAnimation()) {
            if (!d->damageRetryTimer.isActive()) {
                // try again later (otherwise damage is not subtracted)
                d->damageRetryTimer.setInterval(100);
                d->damageRetryTimer.start();
            }
            return;
        }

        // Limit the number of damages we're willing to process if we're
        // in the middle of a transition, so the competition for the GL
        // resources will be less tight.
        if (d->pastDamages) {
            // Forget about pastDamages we received long ago.
            while (d->pastDamages->size() > 0
                   && d->pastDamages->first() + expiry < when)
                d->pastDamages->removeFirst();
            if (d->pastDamages->size() >= limit) {
                // Too many damages in the given timeframe, postpone
                // until the time the queue is ready to accept a new
                // update.
                if (!d->damageRetryTimer.isActive()) {
                    d->damageRetryTimer.setInterval(
                               d->pastDamages->first()+expiry - when);
                    d->damageRetryTimer.start();
                }
                return;
            }
        } else
            d->pastDamages = new QList<Time>;
        // Can afford this damage, but record when we received it,
        // so to know when to forget about them.
        d->pastDamages->append(when);
    } else if (d->pastDamages) {
        // The window is not transitioning, forget about all pastDamages.
        delete d->pastDamages;
        d->pastDamages = NULL;
    }
    d->damageRetryTimer.stop();

    // we want to update the pixmap even if the item is not visible because
    // certain animations require up-to-date pixmap (alternatively we could mark
    // it dirty and update it before the animation starts...)
    if (d->direct_fb_render || propertyCache()->isInputOnly()) {
        propertyCache()->damageSubtract();
        return;
    }

    if (!rects)
        // no rects means the whole area
        d->damageRegion = boundingRect().toRect();
    else {
        QRegion r;
        for (int i = 0; i < num; ++i)
             r += QRegion(rects[i].x, rects[i].y, rects[i].width, rects[i].height);
        d->damageRegion = r;
    }
    
    if (!d->TFP.isValid()) 
        saveBackingStore();
    
    if (!d->damageRegion.isEmpty()) {
        d->TFP.update();
        MCompositeManager *m = (MCompositeManager*)qApp;
        if (!m->disableRedrawingDueToDamage()) {
            if (!d->current_window_group) 
                d->glwidget->update();
            else
                d->current_window_group->updateWindowPixmap();
        }
    }
    propertyCache()->damageSubtract();
}
