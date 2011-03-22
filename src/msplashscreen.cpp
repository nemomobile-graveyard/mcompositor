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

#include "msplashscreen.h"
#include "mcompositemanager.h"
#include "mcompositemanager_p.h"
#include "mdevicestate.h"
#include "mcompositewindowanimation.h"

MSplashPropertyCache *MSplashPropertyCache::singleton;

MSplashPropertyCache::MSplashPropertyCache()
    : MWindowPropertyCache(0)
{
}
MSplashPropertyCache *MSplashPropertyCache::get()
{
    if (!singleton)
        singleton = new MSplashPropertyCache();
    return singleton;
}

bool MSplashPropertyCache::event(QEvent *e)
{
    // Ignore deleteLater().
    return e->type() == QEvent::DeferredDelete ?
                                true : MWindowPropertyCache::event(e);
}

MSplashScreen::MSplashScreen(unsigned int splash_pid, const QString &wmclass,
                             const QString &splash_p, const QString &splash_l,
                             unsigned int splash_pixmap)
    : MTexturePixmapItem(0, MSplashPropertyCache::get()),
      pid(splash_pid),
      wm_class(wmclass),
      portrait_file(splash_p),
      landscape_file(splash_l),
      pixmap(splash_pixmap),
      q_pixmap(0),
      fade_animation(false)
{
    Display *dpy = QX11Info::display();
    win_id = XCreateWindow(dpy, RootWindow(dpy, 0), 0, 0,
                           ScreenOfDisplay(dpy, DefaultScreen(dpy))->width,
                           ScreenOfDisplay(dpy, DefaultScreen(dpy))->height,
                           0, CopyFromParent,
                           InputOnly, CopyFromParent, 0, 0);
    XStoreName(dpy, win_id, "MSplashScreen");
    XMapWindow(dpy, win_id);

    MCompositeManager *m = (MCompositeManager*)qApp;
    if (!pixmap) {
        if (!splash_l.size())
            landscape_file = splash_p;
        // TODO: default path for relative file names
        // TODO 2: mirrored textures for bottom/right
        if (m->d->device_state->screenTopEdge() == "top"
            || m->d->device_state->screenTopEdge() == "bottom") {
            q_pixmap = new QPixmap(landscape_file);
            if (q_pixmap->isNull())
                qWarning() << __func__ << "couldn't load" << landscape_file;
        } else {
            q_pixmap = new QPixmap(portrait_file);
            if (q_pixmap->isNull())
                qWarning() << __func__ << "couldn't load" << portrait_file;
        }
        if (!q_pixmap->isNull())
            pixmap = q_pixmap->handle();
    }

    if (pixmap) {
        timer.setSingleShot(true);
        timer.setInterval(15 * 1000);
        connect(&timer, SIGNAL(timeout()), m->d, SLOT(splashTimeout()));
        timer.start();
    }
    connect(this, SIGNAL(itemIconified(MCompositeWindow*)),
            this, SLOT(iconified()));
}

MSplashScreen::~MSplashScreen()
{
    XDestroyWindow(QX11Info::display(), win_id);
    // NOTE: if pixmap was provided to us, MTexturePixmapItemPrivate frees it.
    // TODO: signal switcher that we freed it

    // return this QPixmap to the QPixmapCache with a valid X pixmap
    delete q_pixmap;
}

const QRegion &MSplashPropertyCache::shapeRegion()
{
    if (bounding_region.isEmpty())
        bounding_region = QApplication::desktop()->geometry();
    return bounding_region;
}

QRectF MSplashScreen::boundingRect() const
{
    return QApplication::desktop()->geometry();
}

bool MSplashScreen::matches(MWindowPropertyCache *pc) const
{
    if (!pc->wmClass()[0].compare(wm_class, Qt::CaseInsensitive))
        return true;
    return false;
}

void MSplashScreen::endAnimation()
{
    if (fade_animation)
        // cross-fade ended, time to go
        ((MCompositeManager*)qApp)->d->splashTimeout();
}

void MSplashScreen::beginAnimation()
{
    if (windowAnimator()->pendingAnimation()
                              == MCompositeWindowAnimation::CrossFade) {
        fade_animation = true;
        // fade started, stop the timer
        timer.stop();
    }
}

void MSplashScreen::iconified()
{
    ((MCompositeManager*)qApp)->d->splashTimeout();
}

