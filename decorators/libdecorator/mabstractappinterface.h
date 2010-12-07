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

class MAbstractAppInterfacePrivate;
class MRmiClient;
class QRect;
class QMenu;


class IPCAction
{
public:
    IPCAction()
        : m_checkable(false)
        , m_checked(false)
    {
    }


    IPCAction(const QAction& act)
        : m_text(act.text())
        , m_checkable(act.isCheckable())
        , m_checked(act.isChecked())
    {
        m_key = QUuid::createUuid();
    }

    QUuid id() const {return m_key; }

    QString text() const {return m_text; }

    bool isCheckable() const {return m_checkable; }

    bool isChecked() const {return m_checked;  }

    friend QDataStream &operator<<(QDataStream &, const IPCAction &);
    friend QDataStream &operator>>(QDataStream &, IPCAction &);

private:

    QUuid m_key;
    QString m_text;
    bool m_checkable;
    bool m_checked;
};

QDataStream &operator<<(QDataStream &out, const IPCAction &myObj);
QDataStream &operator>>(QDataStream &in, IPCAction &myObj);

Q_DECLARE_METATYPE(IPCAction);
Q_DECLARE_METATYPE(QList<IPCAction>);
/*!
 * MAbstractDecorator is the base class for window decorators
 */
class MAbstractAppInterface: public QObject
{
    Q_OBJECT
public:
    /*!
     * Initializes MAbstractDecorator and the connections to MCompositor
     */
    MAbstractAppInterface(QObject *parent = 0);
    virtual ~MAbstractAppInterface() = 0;

    void triggered(IPCAction act, bool val);
    void toggled(IPCAction act, bool val);

public slots:

    void RemoteSetAppMenu(QList<IPCAction> menu);

    void RemoteSetClientKey(const QString& key);

protected:

    virtual void appMenuChanged(QList<IPCAction>) = 0;

private:

    Q_DECLARE_PRIVATE(MAbstractAppInterface)

    MAbstractAppInterfacePrivate * const d_ptr;
};

#endif // MABSTRACTAPPINTERFACE_H
