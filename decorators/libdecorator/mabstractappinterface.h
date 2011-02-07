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

#ifndef MABSTRACTAPPINTERFACE_H
#define MABSTRACTAPPINTERFACE_H

#include <QObject>
#include <QAction>
#include <QUuid>
#include <QDBusArgument>

class MAbstractAppInterfacePrivate;
class MRmiClient;
class QRect;
class QMenu;

/*! The MDecoratorIPCAction class is used to send a QAction over IPC*/
class MDecoratorIPCAction
{
public:
    /*! defines the location of the action*/
    enum ActionType{
        MenuAction,
        ToolBarAction
    };

    MDecoratorIPCAction()
        : m_checkable(false)
        , m_checked(false)
        , m_type(MenuAction)
    {
    }

    MDecoratorIPCAction(const QAction& act, MDecoratorIPCAction::ActionType type)
        : m_text(act.text())
        , m_checkable(act.isCheckable())
        , m_checked(act.isChecked())
        , m_type(type)
        , m_icon(act.icon())
    {
        if (m_text.contains('&'))
            m_text.remove('&');
        m_key = QUuid::createUuid();
    }

    /*! the uniqueid() of the action*/
    QUuid id() const {return m_key; }

    QString text() const {return m_text; }

    bool isCheckable() const {return m_checkable; }

    bool isChecked() const {return m_checked;  }

    MDecoratorIPCAction::ActionType type() const {return m_type; }

    QIcon icon() const {return m_icon; }

    //friend to access the member variables directly because we don't have setter
    friend QDBusArgument &operator<<(QDBusArgument &argument, const MDecoratorIPCAction &action);
    friend const QDBusArgument &operator>>(const QDBusArgument &argument, MDecoratorIPCAction &action);

private:

    QUuid m_key;
    QString m_text;
    bool m_checkable;
    bool m_checked;
    ActionType m_type;
    QIcon m_icon;
};

typedef QList<MDecoratorIPCAction> MDecoratorIPCActionList;

QDBusArgument &operator<<(QDBusArgument &argument, const MDecoratorIPCAction &action);
const QDBusArgument &operator>>(const QDBusArgument &argument, MDecoratorIPCAction &action);

Q_DECLARE_METATYPE(MDecoratorIPCAction);
Q_DECLARE_METATYPE(MDecoratorIPCActionList);

#endif // MABSTRACTAPPINTERFACE_H
