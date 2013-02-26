/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** Copyright (C) 2012 Jolla Ltd.
** Contact: Vesa Halttunen (vesa.halttunen@jollamobile.com)
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation
** and appearing in the file LICENSE.LGPL included in the packaging
** of this file.
**
****************************************************************************/

#include "mdecorator.h"
#include "mdecoratorwindow.h"

MDecorator::MDecorator(MDecoratorWindow *p) : MAbstractDecorator(p),
      decorwindow(p)
{
}

void MDecorator::manageEvent(Qt::HANDLE window, const QString &wmname, int orient, bool, bool hung)
{
    decorwindow->managedWindowChanged(window, wmname, orient, hung);
    setAvailableGeometry(decorwindow->geometry());
}

void MDecorator::hideQueryDialog()
{
    decorwindow->hideQueryDialog();
}

void MDecorator::setOnlyStatusbar(bool)
{
}

void MDecorator::playFeedback(const QString &)
{
}
