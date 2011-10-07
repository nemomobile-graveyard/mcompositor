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

#ifndef MDEVICESTATE_H
#define MDEVICESTATE_H

#include <contextproperty.h>
#ifdef GLES2_VERSION
#include <QtDBus>
#endif
#include <QObject>

/*!
 * This is a class listening to device state that is of interest to
 * MCompositeManager.
 */
class MDeviceState: public QObject
{
    Q_OBJECT

public:

    MDeviceState(QObject* parent = 0);
    ~MDeviceState();

    virtual bool displayOff() const { return display_off; }
    bool ongoingCall() const { return ongoing_call; }
    bool incomingCall() const { return incoming_call; }
    virtual const QString &screenTopEdge() const { return screen_topedge; }
    virtual bool isFlat() const { return is_flat; }
    virtual const QString &touchScreenLock() const
    { return touchScreenLockMode; }

signals:

    void incomingCall();
    void callStateChange(bool call_ongoing);
    void displayStateChange(bool display_off);
    void screenTopEdgeChange(const QString &top_edge);
    void isFlatChange(bool is_flat);

private slots:

#ifdef GLES2_VERSION
    void mceDisplayStatusIndSignal(QString mode);
    void mceTouchScreenLockSignal(QString mode);
    void gotTouchScreenLockMode(QDBusPendingCallWatcher *watcher);
    void gotDisplayStatus(QDBusPendingCallWatcher *watcher);
#endif
    void callPropChanged();
    void topPropChanged();
    void flatPropChanged();

private:

#ifdef GLES2_VERSION
    QDBusConnection *systembus_conn;
    QDBusPendingCallWatcher *tsmode_call;
    QDBusPendingCallWatcher *display_call;
#endif
    ContextProperty *call_prop;
    ContextProperty *top_prop;
    ContextProperty  *flat_prop;
    bool display_off;
    bool ongoing_call;
    bool incoming_call;
    QString screen_topedge;
    QString touchScreenLockMode;
    bool is_flat;
};

#endif
