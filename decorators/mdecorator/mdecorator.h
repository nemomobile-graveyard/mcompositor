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

#ifndef MDECORATOR_H
#define MDECORATOR_H

#include <mabstractdecorator.h>

class MDecoratorWindow;

class MDecorator : public MAbstractDecorator
{
    Q_OBJECT

public:
    MDecorator(MDecoratorWindow *p);

    virtual void manageEvent(Qt::HANDLE window, const QString &wmname, int orient, bool, bool hung);

protected:
    virtual void hideQueryDialog();
    virtual void setOnlyStatusbar(bool);
    virtual void playFeedback(const QString &);

private:
    MDecoratorWindow *decorwindow;
};


#endif // MDECORATOR_H
