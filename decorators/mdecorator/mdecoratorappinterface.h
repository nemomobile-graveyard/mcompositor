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

#ifndef MDECORATORAPPINTERFACE_H
#define MDECORATORAPPINTERFACE_H

#include <mabstractdecorator.h>

class MDecoratorWindow;

class MDecoratorAppInterface : public MAbstractAppInterface
{
    Q_OBJECT

public:
    MDecoratorAppInterface(MDecoratorWindow *p);

    void setManagedWindow(WId window);

protected:
    virtual void actionsChanged(QList<MDecoratorIPCAction>, WId);

private:
    MDecoratorWindow *decorwindow;
    WId currentWindow;
};

#endif // MDECORATORAPPINTERFACE_H
