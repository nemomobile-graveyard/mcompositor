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
** This library is NOT free software! Warning: Internal use only
**
****************************************************************************/

#ifndef MSTATUSBARTEXTURE_H
#define MSTATUSBARTEXTURE_H

#include <QtOpenGL>
#include <X11/extensions/Xdamage.h>

class QDBusServiceWatcher;
class QDBusPendingCallWatcher;
class MCompositeManagerExtension;

// Singleton representation of the status bar texture. Shareable by all objects
class MStatusBarTexture: public QObject
{
    Q_OBJECT
public:
    static MStatusBarTexture* instance();

    GLuint landscapeTexture() const { return texture_id; }
    GLuint portraitTexture() const { return portrait_texture_id; }
    const QRect &landscapeRect() const { return texture_rect; }
    const QRect &portraitRect() const { return texture_rect_portrait; }
    Drawable pixmapDrawable() const { return drawable; }

    void updatePixmap();
    void trackDamages();
    void untrackDamages();

 private slots:
    void statusBarOn();
    void statusBarOff();
    void gotSharedPixmap(QDBusPendingCallWatcher*);

 private:
    explicit MStatusBarTexture(QObject* parent = 0);
    void getSharedPixmap();
    bool updateStatusBarGeometry(QImage &ing);

    QDBusServiceWatcher* dbusWatcher;
    QDBusPendingCallWatcher *pendingCall;

    GLuint texture_id, portrait_texture_id;
    Drawable drawable;
    QRect texture_rect, texture_rect_portrait;
    Damage pixmapDamage;
    static MStatusBarTexture* d;
};

#endif // MSTATUSBARTEXTURE_H
