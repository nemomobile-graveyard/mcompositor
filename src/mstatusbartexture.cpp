/***************************************************************************
**
** Copyright (C) 2010-2011 Nokia Corporation and/or its subsidiary(-ies).
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

#include <QtDBus>
#include <QPixmap>
#include <QX11Info>
#include "mstatusbartexture.h"
#include <mcompositewindow.h>
#include "mcompositemanager.h"

MStatusBarTexture* MStatusBarTexture::d = 0;

const QString PIXMAP_PROVIDER_DBUS_SERVICE = "com.meego.core.MStatusBar";
const QString PIXMAP_PROVIDER_DBUS_PATH = "/statusbar";
const QString PIXMAP_PROVIDER_DBUS_INTERFACE = "com.meego.core.MStatusBar";
const QString PIXMAP_PROVIDER_DBUS_SHAREDPIXMAP_CALL = "sharedPixmapHandle";

MStatusBarTexture* MStatusBarTexture::instance()
{
    if (!d)
        d = new MStatusBarTexture();
    return d;
}

MStatusBarTexture::MStatusBarTexture(QObject *parent)
    :QObject(parent),
     pendingCall(0),
     drawable(0),
     pixmapDamage(0)
{
    dbusWatcher = new QDBusServiceWatcher(PIXMAP_PROVIDER_DBUS_SERVICE,
                                          QDBusConnection::sessionBus(),
                                          QDBusServiceWatcher::WatchForRegistration |
                                          QDBusServiceWatcher::WatchForUnregistration,
                                          this);

    connect(dbusWatcher, SIGNAL(serviceRegistered(QString)),
            this, SLOT(statusBarOn()));
    connect(dbusWatcher, SIGNAL(serviceUnregistered(QString)),
            this, SLOT(statusBarOff()));

    if (!QGLContext::currentContext()) {
        qWarning("MStatusBarTexture::%s(): no current GL context",
                 __func__);
        return;
    }

    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &portrait_texture_id);
    glBindTexture(GL_TEXTURE_2D, portrait_texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    getSharedPixmap();
}

void MStatusBarTexture::getSharedPixmap()
{
    // In case a method call is already in progress, resubmit it.
    delete pendingCall;
    pendingCall = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(
            QDBusMessage::createMethodCall(PIXMAP_PROVIDER_DBUS_SERVICE,
                                           PIXMAP_PROVIDER_DBUS_PATH,
                                           PIXMAP_PROVIDER_DBUS_INTERFACE,
                                           PIXMAP_PROVIDER_DBUS_SHAREDPIXMAP_CALL)),
        this);
    connect(pendingCall, SIGNAL(finished(QDBusPendingCallWatcher*)),
            this, SLOT(gotSharedPixmap(QDBusPendingCallWatcher*)));
}

void MStatusBarTexture::trackDamages()
{
    if (pixmapDamage == None)
        pixmapDamage = XDamageCreate(QX11Info::display(), drawable,
                                     XDamageReportNonEmpty);
}

void MStatusBarTexture::untrackDamages()
{
    if (pixmapDamage) {
        XDamageDestroy(QX11Info::display(), pixmapDamage);
        pixmapDamage = None;
    }
}

bool MStatusBarTexture::updateStatusBarGeometry(QImage &img)
{
    if (!drawable)
        return false;
    QPixmap pixmap = QPixmap::fromX11Pixmap(drawable, QPixmap::ExplicitlyShared);
    //pixmap.save("shot.png");

    QT_TRY {
        // Assume we need inverted textures.
        img = QImage(pixmap.toImage().mirrored());
    } QT_CATCH(std::bad_alloc e) {
        // @drawable has become invalid
        statusBarOff();
        return false;
    }

    // @img depicts both the landscape and the portrait statusbars,
    // one right below the other and both pictured horizontally.
    // The size of @img doesn't necessarily implies the size of the
    // contained status bars, so we need to figure out their size.
    // It would be straightforward if they were plain old X windows,
    // but that's and old story in a universe far-far away.
    QSize lscape, portrait;

    MCompositeWindow *desktop =
        MCompositeWindow::compositeWindow(((MCompositeManager *)qApp)->desktopWindow());
    if (desktop) {
        lscape = desktop->propertyCache()->statusbarGeometry().size();
        if (!lscape.isEmpty()
                   && desktop->propertyCache()->orientationAngle() % 180) {
            // @lscape contains the transposed size of the portrait statusbar.
            lscape.transpose();
            portrait = lscape;

            // Assume full-width landscape statusbar.
            lscape.setWidth(QApplication::desktop()->width());
            if (lscape.width() > img.width())
                lscape.setWidth(img.width());
        }
    }

    if (lscape.isEmpty())
        // Couldn't determine the size from the desktop window,
        // assume some probable defaults.
        lscape = QSize(img.width(),
            ((MCompositeManager*)qApp)->configInt("default-statusbar-height"));
    if (portrait.isEmpty())
        // Assume full-width portrait statusbar.
        portrait = QSize(QApplication::desktop()->height(), lscape.height());

    texture_rect = QRect(QPoint(0, 0), lscape);
    texture_rect_portrait = QRect(QPoint(0, 0), portrait);

    return true;
}

void MStatusBarTexture::updatePixmap()
{
    if (!QGLContext::currentContext()) {
        qWarning("MStatusBarTexture::%s(): no current GL context",
                 __func__);
        return;
    }

    QImage img;
    if (!updateStatusBarGeometry(img))
        return;
    img = QGLWidget::convertToGLFormat(img);

    glBindTexture(GL_TEXTURE_2D, texture_id);
    QImage statimg = img.copy(QRect(texture_rect));
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 statimg.width(), statimg.height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, statimg.bits());

    glBindTexture(GL_TEXTURE_2D, portrait_texture_id);
    statimg = img.copy(QRect(0, texture_rect.height(),
                             texture_rect_portrait.width(),
                             texture_rect_portrait.height()));
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 statimg.width(), statimg.height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, statimg.bits());

    trackDamages();
}

void MStatusBarTexture::gotSharedPixmap(QDBusPendingCallWatcher* d)
{
    QDBusPendingReply<quint32> reply = *d;
    if (reply.isError()) {
        qDebug() << __func__ << reply.error().message();
        if (reply.error().type() == QDBusError::NoReply)
            // No reply in 25 seconds, try again.
            getSharedPixmap();
        return;
    }

    drawable = reply;
    if (pixmapDamage != None) {
        // Update the tex
        untrackDamages();
        updatePixmap();
    } else {
        // Not important now.
        QImage img;
        updateStatusBarGeometry(img);
    }
}

void MStatusBarTexture::statusBarOn()
{
    getSharedPixmap();
}

void MStatusBarTexture::statusBarOff()
{
    untrackDamages();
    drawable = None;
    texture_rect = texture_rect_portrait = QRect();
}
