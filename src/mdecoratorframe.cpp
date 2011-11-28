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

#include "mdecoratorframe.h"
#include "mcompositewindow.h"
#include "mtexturepixmapitem.h"
#include "mcompositemanager.h"
#include "mcompositordebug.h"
#include "mrmi.h"

#include <QX11Info>

#include <X11/Xutil.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xmd.h>

MDecoratorFrame *MDecoratorFrame::d = 0;

MDecoratorFrame::MDecoratorFrame(QObject *p)
    : QObject(p),
      client(0),
      decorator_window(0),
      decorator_item(0),
      sent_orientation(-1),
      sent_show_dialog(false),
      no_resize(false)
{    
    // One instance at a time
    Q_ASSERT(!d);
    d = this;

    remote_decorator = new MRmiClient(".mabstractdecorator", this);
    remote_decorator->exportObject(this);
}

Qt::HANDLE MDecoratorFrame::managedWindow() const
{
    return client ? client->window() : 0;
}

Qt::HANDLE MDecoratorFrame::winId() const
{
    return decorator_window;
}

void MDecoratorFrame::hide()
{
    if (decorator_item)
        decorator_item->setVisible(false);
}

void MDecoratorFrame::show()
{
    if (decorator_item)
        decorator_item->setVisible(true);
}

void MDecoratorFrame::sendManagedWindowId(bool show_dialog)
{
    QVector<QVariant> args;

    if (client) {
        MWindowPropertyCache *pc = client->propertyCache();
        args << unsigned(client->window())
             << pc->requestedGeometry()
             << pc->wmName()
             << pc->orientationAngle()
             << only_statusbar
             << show_dialog;
        sent_orientation = pc->orientationAngle();
        sent_show_dialog = show_dialog;
    } else {
        args << unsigned(0) << QRect() << QString() << unsigned(0)
             << false << false;
        sent_orientation = -1;
        sent_show_dialog = false;
    }

    remote_decorator->invoke("MAbstractDecorator", "RemoteSetManagedWinId",
                             args);
}

void MDecoratorFrame::setManagedWindow(MCompositeWindow *cw,
                                       bool no_resize,
                                       bool only_statusbar,
                                       bool show_dialog)
{    
    this->no_resize = no_resize;
    this->only_statusbar = only_statusbar;

    if (client == cw) {
        if (cw && (show_dialog != sent_show_dialog ||
            sent_orientation != (int)cw->propertyCache()->orientationAngle()))
            // Time to @show_dialog again or orientation changed.
            sendManagedWindowId(show_dialog);
        return;
    }

    if (client)
        disconnect(client, SIGNAL(destroyed()), this, SLOT(destroyClient()));
    client = cw;

    if (decorator_item)
        sendManagedWindowId(show_dialog);
    if (cw) {
        if (!no_resize)
            cw->expectResize();
        connect(cw, SIGNAL(destroyed()), SLOT(destroyClient()));
    }
}

void MDecoratorFrame::showQueryDialog(MCompositeWindow *cw,
                                      bool only_statusbar)
{
    setManagedWindow(cw, true, only_statusbar, true);
}

void MDecoratorFrame::hideQueryDialog()
{
    remote_decorator->invoke("MAbstractDecorator",
                             "RemoteHideQueryDialog");
}

void MDecoratorFrame::playFeedback(const QString &name)
{
    remote_decorator->invoke("MAbstractDecorator",
                             "RemotePlayFeedback", name);
}

void MDecoratorFrame::setOnlyStatusbar(bool mode)
{
    only_statusbar = mode;
    remote_decorator->invoke("MAbstractDecorator",
                             "RemoteSetOnlyStatusbar", mode);
}

void MDecoratorFrame::queryDialogAnswer(unsigned window, bool killit)
{
    static_cast<MCompositeManager *>(qApp)->queryDialogAnswer(window, killit);
}

void MDecoratorFrame::setDecoratorWindow(Qt::HANDLE window)
{
    decorator_window = window;
    XMapWindow(QX11Info::display(), window);
}

void MDecoratorFrame::setDecoratorItem(MCompositeWindow *window)
{
    decorator_item = window;
    if (!window)
        return;
    connect(decorator_item, SIGNAL(destroyed()), SLOT(destroyDecorator()));

    MTexturePixmapItem *item = (MTexturePixmapItem *) window;
    if (!decorator_window)
        setDecoratorWindow(item->window());
    sendManagedWindowId(sent_show_dialog);
}

MCompositeWindow *MDecoratorFrame::decoratorItem() const
{
    return decorator_item;
}

void MDecoratorFrame::destroyDecorator()
{
    decoratorRectChanged(QRect());
    decorator_item = 0;
    decorator_window = 0;
}

void MDecoratorFrame::destroyClient()
{
    if (client == sender())
        client = 0;
}

void MDecoratorFrame::decoratorRectChanged(const QRect& r)
{    
    // always store the available rect info from remote decorator
    available_rect = r;

    if (!client || no_resize || !decorator_item || r.isEmpty()
        || !decorator_item->propertyCache())
        return;
    
    if (client->propertyCache()->realGeometry() != available_rect) {
      // resize app window to occupy the free area
      XMoveResizeWindow(QX11Info::display(), client->window(),
                        r.x(), r.y(), r.width(), r.height());
      MOVE_RESIZE(client->window(), r.x(), r.y(), r.width(), r.height());
      static_cast<MCompositeManager *>(qApp)->expectResize(client, r);
    }
}
