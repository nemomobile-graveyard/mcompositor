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

#include "mdecoratorappinterface.h"
#include "mdecoratorwindow.h"

MDecoratorAppInterface::MDecoratorAppInterface(MDecoratorWindow *p) : MAbstractAppInterface(p),
    decorwindow(p)
{
}

void MDecoratorAppInterface::setManagedWindow(WId window)
{
    currentWindow = window;
}

void MDecoratorAppInterface::actionsChanged(QList<MDecoratorIPCAction>, WId)
{
}
