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

void MDecoratorFrame::sendManagedWindowId()
{
    qulonglong winid = client ? client->window() : 0;
    if(client)
        remote_decorator->invoke("MAbstractDecorator",
                                 "RemoteSetClientGeometry",
                                 client->propertyCache()->requestedGeometry());
    remote_decorator->invoke("MAbstractDecorator",
                             "RemoteSetAutoRotation", false);
    remote_decorator->invoke("MAbstractDecorator",
                             "RemoteSetManagedWinId", winid);
}

void MDecoratorFrame::setManagedWindow(MCompositeWindow *cw, bool no_resize)
{    
    this->no_resize = no_resize;

    if (client == cw)
        return;
    if (client)
        disconnect(client, SIGNAL(destroyed()), this, SLOT(destroyClient()));
    client = cw;

    if (decorator_item)
        sendManagedWindowId();
    if (cw) {
        if (!no_resize)
            cw->expectResize();
        connect(cw, SIGNAL(destroyed()), SLOT(destroyClient()));
    }
}

void MDecoratorFrame::setDecoratorWindow(Qt::HANDLE window)
{
    decorator_window = window;
    XMapWindow(QX11Info::display(), window);
}

void MDecoratorFrame::setDecoratorItem(MCompositeWindow *window)
{
    decorator_item = window;
    connect(decorator_item, SIGNAL(destroyed()), SLOT(destroyDecorator()));

    MTexturePixmapItem *item = (MTexturePixmapItem *) window;
    if (!decorator_window)
        setDecoratorWindow(item->window());
    sendManagedWindowId();
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

void MDecoratorFrame::visualizeDecorator(bool visible)
{
    if (sender() == decorator_item)
        return;

    if (decorator_item)
        decorator_item->setVisible(visible);
}

void MDecoratorFrame::decoratorRectChanged(const QRect& r)
{    
    // always store the available rect info from remote decorator
    available_rect = r;

    if (!client || no_resize || !decorator_item
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

void MDecoratorFrame::queryDialogAnswer(unsigned window, bool killit)
{
    static_cast<MCompositeManager *>(qApp)->queryDialogAnswer(window, killit);
}

void MDecoratorFrame::setAutoRotation(bool mode)
{
    remote_decorator->invoke("MAbstractDecorator",
                             "RemoteSetAutoRotation", mode);
}

void MDecoratorFrame::setOnlyStatusbar(bool mode)
{
    remote_decorator->invoke("MAbstractDecorator",
                             "RemoteSetOnlyStatusbar", mode);
}

void MDecoratorFrame::showQueryDialog(bool visible)
{
    remote_decorator->invoke("MAbstractDecorator",
                             "RemoteShowQueryDialog", visible);
}
