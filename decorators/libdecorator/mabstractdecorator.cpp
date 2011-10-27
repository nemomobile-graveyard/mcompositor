/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (directui@nokia.com)
**
** This file is part of duicompositor.
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

#include <QtDebug>

#include "mabstractdecorator.h"
#include "mrmi.h"

#include <QX11Info>
#include <QRect>
#include <QRegion>
#include <QDesktopWidget>
#include <QApplication>

#include <X11/Xutil.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xmd.h>

struct MAbstractDecoratorPrivate
{    
    Qt::HANDLE client;
    MRmiServer* rmi;
    QRect clientGeometry;
    MAbstractDecorator* q_ptr;
};

MAbstractDecorator::MAbstractDecorator(QObject *parent)
    : QObject(parent),
      d_ptr(new MAbstractDecoratorPrivate())
{
    Q_D(MAbstractDecorator);
    
    d->rmi = new MRmiServer(".mabstractdecorator", this);
    d->rmi->exportObject(this);
}

MAbstractDecorator::~MAbstractDecorator()
{
}

Qt::HANDLE MAbstractDecorator::managedWinId()
{
    Q_D(MAbstractDecorator);
    
    return d->client;
}

void MAbstractDecorator::RemoteSetManagedWinId(unsigned window,
                                               const QRect &geo,
                                               const QString &wmname,
                                               unsigned angle,
                                               bool only_statusbar,
                                               bool show_dialog)
{
    Q_D(MAbstractDecorator);
    M::OrientationAngle orient;

    d->client = window;
    d->clientGeometry = geo;

    if (angle == 0)
        orient = M::Angle0;
    else if (angle == 270)
        orient = M::Angle270;
    else if (angle == 90)
        orient = M::Angle90;
    else if (angle == 180)
        orient = M::Angle180;
    else
        orient = M::Angle0;

    manageEvent(window, wmname, orient, only_statusbar, show_dialog);
}

void MAbstractDecorator::setAvailableGeometry(const QRect& rect)
{
    Q_D(MAbstractDecorator);

    d->rmi->invoke("MDecoratorFrame", "decoratorRectChanged", rect);
}

void MAbstractDecorator::queryDialogAnswer(unsigned int w, bool a)
{
    Q_D(MAbstractDecorator);

    d->rmi->invoke("MDecoratorFrame", "queryDialogAnswer",
                   QVector<QVariant>() << w << a);
}

void MAbstractDecorator::RemoteSetOnlyStatusbar(bool mode)
{
    setOnlyStatusbar(mode);
}

void MAbstractDecorator::RemoteHideQueryDialog()
{
    hideQueryDialog();
}

void MAbstractDecorator::RemotePlayFeedback(const QString &name)
{
    playFeedback(name);
}
