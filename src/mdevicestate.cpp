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

#ifdef GLES2_VERSION
#include <QtDBus>
#include <mce/dbus-names.h>
#include <mce/mode-names.h>
#endif

#include "mdevicestate.h"

#ifdef GLES2_VERSION
void MDeviceState::mceDisplayStatusIndSignal(QString mode)
{
    if (mode == MCE_DISPLAY_OFF_STRING) {
        display_off = true;
        emit displayStateChange(true);
    } else {  // "on" or "dimmed"
        if (display_off) {
            display_off = false;
            emit displayStateChange(false);
        }
    }
}

void MDeviceState::mceTouchScreenLockSignal(QString mode)
{
    if (tsmode_call) {
        delete tsmode_call;
        tsmode_call = 0;
    }
    touchScreenLockMode = mode;
}

void MDeviceState::gotTouchScreenLockMode(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QString> reply = *watcher;
    if (reply.isError()) {
        qDebug() << __func__ << "getting touch screen lock mode failed:"
                 << reply.error().message();
        goto away;
    }
    touchScreenLockMode = reply;

away:
    delete tsmode_call;
    tsmode_call = 0;
}
#endif

void MDeviceState::callPropChanged()
{
    QString val = call_prop->value().toString();
    incoming_call = false;
    if (val == "active") {
        ongoing_call = true;
        emit callStateChange(true);
    } else if (val == "alerting") {
        incoming_call = true;
        ongoing_call = false;
    } else {
        ongoing_call = false;
        emit callStateChange(false);
    }
}

void MDeviceState::topPropChanged()
{
    screen_topedge = top_prop->value().toString();
    emit screenTopEdgeChange(screen_topedge);
}

MDeviceState::MDeviceState(QObject* parent)
    : QObject(parent),
      ongoing_call(false),
      incoming_call(false),
      screen_topedge("top")
{
    display_off = false;

    call_prop = new ContextProperty("Phone.Call");
    connect(call_prop, SIGNAL(valueChanged()), this, SLOT(callPropChanged()));
    top_prop = new ContextProperty("Screen.TopEdge");
    connect(top_prop, SIGNAL(valueChanged()), this, SLOT(topPropChanged()));

#ifdef GLES2_VERSION
    touchScreenLockMode = MCE_TK_UNLOCKED;
    systembus_conn = new QDBusConnection(QDBusConnection::systemBus());
    if (!systembus_conn->isConnected())
        qWarning("Failed to connect to the D-Bus system bus");

    systembus_conn->connect(MCE_SERVICE, MCE_SIGNAL_PATH, MCE_SIGNAL_IF,
                            MCE_DISPLAY_SIG, this,
                            SLOT(mceDisplayStatusIndSignal(QString)));
    systembus_conn->connect(MCE_SERVICE, MCE_SIGNAL_PATH, MCE_SIGNAL_IF,
                            MCE_TKLOCK_MODE_SIG, this,
                            SLOT(mceTouchScreenLockSignal(QString)));

    // get the initial state of touch screen lock
    tsmode_call = new QDBusPendingCallWatcher(
        systembus_conn->asyncCall(
            QDBusMessage::createMethodCall(MCE_SERVICE,
                                           MCE_REQUEST_PATH,
                                           MCE_REQUEST_IF,
                                           MCE_TKLOCK_MODE_GET)), this);
    connect(tsmode_call, SIGNAL(finished(QDBusPendingCallWatcher*)),
            this, SLOT(gotTouchScreenLockMode(QDBusPendingCallWatcher*)));

    /* FIXME: Temporary workaround, current MCE does not seem to provide
     * get_display_status interface */
    QFile file("/sys/class/backlight/himalaya/brightness");
    if (file.open(QIODevice::ReadOnly)) {
        char buf[50];
        qint64 len = file.readLine(buf, sizeof(buf));
        buf[49] = '\0';
        if (len != -1) {
            int i = atoi(buf);
            if (i == 0) display_off = true;
        }
        file.close();
    }
#endif
}

MDeviceState::~MDeviceState()
{
    delete call_prop;
    call_prop = 0;
}
