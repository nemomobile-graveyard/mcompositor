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
#include <mstatusbartexture.h>
#include <mtexturefrompixmap.h>
#include <mcompositewindow.h>
#include "mcompositemanager.h"

MStatusBarTexture* MStatusBarTexture::d = 0;

const QString PIXMAP_PROVIDER_DBUS_SERVICE = "com.meego.core.MStatusBar";
const QString PIXMAP_PROVIDER_DBUS_PATH = "/statusbar";
const QString PIXMAP_PROVIDER_DBUS_INTERFACE = "com.meego.core.MStatusBar";
const QString PIXMAP_PROVIDER_DBUS_SHAREDPIXMAP_CALL = "sharedPixmapHandle";

static void calculate_texture_coords(GLfloat coords[8], GLfloat w, GLfloat h,
                                     GLfloat tx, GLfloat ty, GLfloat tw, GLfloat th)
{
    tx = tx / w;
    ty = ty / h;
    tw = tw / w;
    th = th / h;
    
    coords[0] = tx;      coords[1] = ty;
    coords[2] = tx;      coords[3] = th + ty;
    coords[4] = tx + tw; coords[5] = th + ty;
    coords[6] = tx + tw; coords[7] = ty;
}

MStatusBarTexture* MStatusBarTexture::instance()
{
    if (!d)
        d = new MStatusBarTexture();
    return d;
}

MStatusBarTexture::MStatusBarTexture(QObject *parent)
    :QObject(parent),
     pendingCall(0),
     pixmapDamage(0),
     TFP(new MTextureFromPixmap()),
     size_needs_update(true)
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

    glGenTextures(1, &TFP->textureId);
    glBindTexture(GL_TEXTURE_2D, TFP->textureId);
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
        pixmapDamage = XDamageCreate(QX11Info::display(), TFP->drawable,
                                     XDamageReportNonEmpty);
}

void MStatusBarTexture::untrackDamages()
{
    if (pixmapDamage) {
        XDamageDestroy(QX11Info::display(), pixmapDamage);
        pixmapDamage = None;
    }
}

bool MStatusBarTexture::updateStatusBarGeometry()
{
    if (!size_needs_update)
        return false;
    
    if (!TFP->drawable)
        return false;
    
    QPixmap pixmap = QPixmap::fromX11Pixmap(TFP->drawable, QPixmap::ExplicitlyShared);    
    QSize lscape, portrait;

    MCompositeWindow *desktop =
        MCompositeWindow::compositeWindow(((MCompositeManager *)qApp)->desktopWindow());
    if (desktop) {
        size_needs_update = false;
        lscape = desktop->propertyCache()->statusbarGeometry().size();
        if (!lscape.isEmpty()
                   && desktop->propertyCache()->orientationAngle() % 180) {
            // @lscape contains the transposed size of the portrait statusbar.
            lscape.transpose();
            portrait = lscape;

            // Assume full-width landscape statusbar.
            lscape.setWidth(QApplication::desktop()->width());
            if (lscape.width() > pixmap.width())
                lscape.setWidth(pixmap.width());
        }
    }

    if (lscape.isEmpty())
        // Couldn't determine the size from the desktop window,
        // assume some probable defaults.
        lscape = QSize(pixmap.width(),
            ((MCompositeManager*)qApp)->configInt("default-statusbar-height"));
    if (portrait.isEmpty())
        // Assume full-width portrait statusbar.
        portrait = QSize(QApplication::desktop()->height(), lscape.height());

    texture_rect = QRect(QPoint(0, 0), lscape);
    texture_rect_portrait = QRect(QPoint(0, 0), portrait);
    
    calculate_texture_coords(texture_coords, pixmap.width(), pixmap.height(),
                             texture_rect.x(), texture_rect.y(),
                             texture_rect.width(), texture_rect.height());
    
    calculate_texture_coords(texture_coords_portrait, pixmap.width(), pixmap.height(),
                             texture_rect_portrait.x(), texture_rect_portrait.y() + texture_rect.height(),
                             texture_rect_portrait.width(), texture_rect_portrait.height());
    
    return true;
}

void MStatusBarTexture::updatePixmap()
{
    if (!QGLContext::currentContext()) {
        qWarning("MStatusBarTexture::%s(): no current GL context",
                 __func__);
        return;
    }

    if (!updateStatusBarGeometry())
        return;
    TFP->update();
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

    TFP->bind(reply);
    size_needs_update = true;
    if (pixmapDamage != None) {
        // Update the tex
        untrackDamages();
        updatePixmap();
    } else {
        // Not important now.
        updateStatusBarGeometry();
    }
}

void MStatusBarTexture::statusBarOn()
{
    getSharedPixmap();
}

void MStatusBarTexture::statusBarOff()
{
    untrackDamages();
    TFP->unbind();
    texture_rect = texture_rect_portrait = QRect();
}

GLuint MStatusBarTexture::texture() const { return TFP->textureId; }

Drawable MStatusBarTexture::pixmapDrawable() const { return TFP->drawable; }
