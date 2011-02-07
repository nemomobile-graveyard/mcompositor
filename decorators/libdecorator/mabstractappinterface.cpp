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

#include "mabstractappinterface.h"
#include "mdecorator_dbus_adaptor.h"
#include "mabstractdecorator.h"
#include <mrmiserver.h>
#include <mrmiclient.h>
#include <QX11Info>
#include <QRect>
#include <QRegion>
#include <QDesktopWidget>
#include <QApplication>
#include <QMenu>
#include <QPixmap>


QDBusArgument &operator<<(QDBusArgument &argument, const MDecoratorIPCAction &action)
{
    argument.beginStructure();
    argument << action.m_key.toString();
    argument << action.m_text;
    argument << action.m_checkable;
    argument << action.m_checked;
    argument << (uint)action.m_type;

    if (action.m_icon.isNull()) {
        argument << QByteArray();
    } else {
        if (action.m_icon.pixmap(48,48).isNull())
            qCritical() << "MDecorator: Pixmap creation failed";

        QImage image = action.m_icon.pixmap(48,48).toImage();
        if (image.isNull())
            qCritical() << "MDecorator: Icon Conversion failed";
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer,"PNG"))
            qCritical() << "MDecorator: Write to Buffer failed";
        argument << data;
    }

    argument.endStructure();
    return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, MDecoratorIPCAction &action)
{
    argument.beginStructure();
    int type;
    QString uuid;
    argument >> uuid;
    action.m_key = QUuid(uuid);
    argument >> action.m_text;
    argument >> action.m_checkable;
    argument >> action.m_checked;
    argument >> type;
    QByteArray data;
    argument >> data;
    if(!data.isNull()){
        QImage image = QImage::fromData(data,"PNG");
        if (image.isNull())
            qCritical() << "MDecorator: Icon loading failed";
        action.m_icon = QIcon(QPixmap::fromImage(image));
    }
    action.m_type = (MDecoratorIPCAction::ActionType)type;
    argument.endStructure();
    return argument;
}

class MAbstractAppInterfacePrivate
{
public:

    MDecoratorAdaptor* adaptor;
    MAbstractAppInterface* q_ptr;
};

MAbstractAppInterface::MAbstractAppInterface(QObject *parent)
    : QObject(parent),
      d_ptr(new MAbstractAppInterfacePrivate())
{
    qDBusRegisterMetaType<MDecoratorIPCAction>();
    qDBusRegisterMetaType<MDecoratorIPCActionList>();

    d_ptr->adaptor = new MDecoratorAdaptor(this);

    QDBusConnection::sessionBus().registerService("com.nokia.MDecorator");
    QDBusConnection::sessionBus().registerObject("/MDecorator", this);
}

MAbstractAppInterface::~MAbstractAppInterface()
{
}

void MAbstractAppInterface::setActions(MDecoratorIPCActionList menu ,uint window)
{
    actionsChanged(menu, (WId)window);
}

