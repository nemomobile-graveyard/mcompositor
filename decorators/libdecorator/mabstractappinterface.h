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

/*! The IPCAction class is used to send a QAction over IPC*/
class IPCAction
{
public:
    /*! defines the location of the action*/
    enum ActionType{
        MenuAction,
        ToolBarAction
    };

    IPCAction()
        : m_checkable(false)
        , m_checked(false)
        , m_type(MenuAction)
    {
    }

    IPCAction(const QAction& act, IPCAction::ActionType type)
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

    IPCAction::ActionType type() const {return m_type; }

    QIcon icon() const {return m_icon; }

    //friend to access the member variables directly because we don't have setter
    friend QDBusArgument &operator<<(QDBusArgument &argument, const IPCAction &action);
    friend const QDBusArgument &operator>>(const QDBusArgument &argument, IPCAction &action);

private:

    QUuid m_key;
    QString m_text;
    bool m_checkable;
    bool m_checked;
    ActionType m_type;
    QIcon m_icon;
};

typedef QList<IPCAction> IPCActionList;

QDBusArgument &operator<<(QDBusArgument &argument, const IPCAction &action);
const QDBusArgument &operator>>(const QDBusArgument &argument, IPCAction &action);

Q_DECLARE_METATYPE(IPCAction);
Q_DECLARE_METATYPE(IPCActionList);
/*!
 * MAbstractAppInterface is the base class for Application Interface

   It is used to communicate to the current decorated Application.
 */
class MAbstractAppInterface: public QObject
{
    Q_OBJECT
public:
    /*!
     * Initializes MAbstractAppInterface and listens for Messages from the Application
     */
    MAbstractAppInterface(QObject *parent = 0);
    virtual ~MAbstractAppInterface() = 0;

signals:
    /*! Sends the triggered signal for the given Action to the current decorated Application*/
    void triggered(QString id, bool val);
    /*! Sends the toggled signal for the given Action to the current decorated Application*/
    void toggled(QString id, bool val);

public slots:

    /*! set the List of Actions in the Menu/ToolBar of the decorator. The window is used
        to check for the current decorated window */
    void setActions(IPCActionList ,uint window);

protected:

    virtual void actionsChanged(QList<IPCAction>, WId window) = 0;

private:

    Q_DECLARE_PRIVATE(MAbstractAppInterface)

    MAbstractAppInterfacePrivate * const d_ptr;
};

#endif // MABSTRACTAPPINTERFACE_H
