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

#include <QX11Info>
#include <QKeySequence>
#include <QKeyEvent>
#include <QEvent>
#include <QTimer>
#include <QApplication>
#include <QDesktopWidget>

#include "mcompositewindow.h"
#include "mcompositescene.h"

#include <X11/extensions/Xfixes.h>
#ifdef HAVE_SHAPECONST
#include <X11/extensions/shapeconst.h>
#else
#include <X11/extensions/shape.h>
#endif
#include <X11/extensions/Xcomposite.h>

static int error_handler(Display * , XErrorEvent *error)
{
    if (error->resourceid == QX11Info::appRootWindow() && error->error_code == BadAccess) {
        qCritical("Another window manager is running.");
        ::exit(0);
    }
    if (error->error_code == BadMatch)
        qDebug() << "Bad match error " << error->resourceid;

    return 0;
}

MCompositeScene::MCompositeScene(QObject *p)
    : QGraphicsScene(p)
{
    setBackgroundBrush(Qt::NoBrush);
    setForegroundBrush(Qt::NoBrush);
    setSceneRect(QRect(0, 0,
                       QApplication::desktop()->width(),
                       QApplication::desktop()->height()));
    installEventFilter(this);
}

void MCompositeScene::prepareRoot()
{
    Display *dpy = QX11Info::display();
    Window root =  QX11Info::appRootWindow();

    XSetWindowAttributes sattr;
    sattr.event_mask =  SubstructureRedirectMask | SubstructureNotifyMask | StructureNotifyMask | PropertyChangeMask;

    //XCompositeRedirectSubwindows (dpy, root, CompositeRedirectAutomatic);

    XChangeWindowAttributes(dpy, root, CWEventMask, &sattr);
    XSelectInput(dpy, root, SubstructureNotifyMask | SubstructureRedirectMask | StructureNotifyMask | PropertyChangeMask);
    XSetErrorHandler(error_handler);
}


void MCompositeScene::setupOverlay(Window window, const QRect &geom,
                                     bool restoreInput)
{
    Display *dpy = QX11Info::display();
    XRectangle rect;

    rect.x      = geom.x();
    rect.y      = geom.y();
    rect.width  = geom.width();
    rect.height = geom.height();
    XserverRegion region = XFixesCreateRegion(dpy, &rect, 1);

    XFixesSetWindowShapeRegion(dpy, window, ShapeBounding, 0, 0, 0);
    if (!restoreInput)
        XFixesSetWindowShapeRegion(dpy, window, ShapeInput, 0, 0, region);
    else
        XFixesSetWindowShapeRegion(dpy, window, ShapeInput, 0, 0, 0);

    XFixesDestroyRegion(dpy, region);
}

void MCompositeScene::drawItems(QPainter *painter, int numItems, QGraphicsItem *items[], const QStyleOptionGraphicsItem options[], QWidget *widget)
{
    QRegion visible(sceneRect().toRect());
    QVector<int> to_paint(10);
    int size = 0;
    // visibility is determined from top to bottom
    for (int i = numItems - 1; i >= 0; --i) {
        MCompositeWindow *cw = (MCompositeWindow *) items[i];
        
        if (cw->isDirectRendered() || !cw->isVisible()
            || !cw->propertyCache()->isMapped()
            || cw->propertyCache()->isInputOnly())
            continue;
        if (visible.isEmpty())
            // nothing below is visible anymore
            break;

        // FIXME: this region is always the same as the window's shape,
        // some transformations would be needed...
        QRegion r(cw->sceneMatrix().map(cw->propertyCache()->shapeRegion()));
        
        // transitioning window can be smaller than shapeRegion(), so paint
        // all transitioning windows
        if (cw->isWindowTransitioning() || visible.intersects(r)) {
            if (size >= 9)
                to_paint.resize(to_paint.size()+1);
            to_paint[size++] = i;
        }

        // subtract opaque regions
        if (!cw->isWindowTransitioning()
            && !cw->propertyCache()->hasAlpha() && cw->opacity() == 1.0)
            visible -= r;
    }
    if (size > 0) {
        // paint from bottom to top so that blending works
        for (int i = size - 1; i >= 0; --i) {
            int item_i = to_paint[i];
            painter->save();
            // TODO: paint only the intersected region (glScissor?)
            painter->setMatrix(items[item_i]->sceneMatrix(), true);
            items[item_i]->paint(painter, &options[item_i], widget);
            painter->restore();
        }
    }
}
