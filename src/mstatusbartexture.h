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

#ifndef MSTATUSBARTEXTURE_H
#define MSTATUSBARTEXTURE_H

#include <QtOpenGL>
#include <X11/extensions/Xdamage.h>

#include <texturecoords.h>

class QDBusServiceWatcher;
class QDBusPendingCallWatcher;
class MCompositeManagerExtension;
class MTextureFromPixmap;

// Singleton representation of the status bar texture. Shareable by all objects
class MStatusBarTexture: public QObject
{
    Q_OBJECT
public:
    static MStatusBarTexture* instance();
    GLuint texture() const;
    const QRect &landscapeRect() const { return texture_rect; }
    const QRect &portraitRect() const { return texture_rect_portrait; }
    Drawable pixmapDrawable() const;
    const TextureCoords& landscapeTexCoords() const { return texture_coords; }
    const TextureCoords& portraitTexCoords() const { return texture_coords_portrait; }

    void updatePixmap();
    void trackDamages();
    void untrackDamages();

 signals:
    /* emmited whenever status bar geometry is updated. */
    void geometryUpdated();

 private slots:
    void statusBarOn();
    void statusBarOff();
    void gotSharedPixmap(QDBusPendingCallWatcher*);

 private:
    explicit MStatusBarTexture(QObject* parent = 0);
    void getSharedPixmap();
    bool updateStatusBarGeometry();

    QDBusServiceWatcher* dbusWatcher;
    QDBusPendingCallWatcher *pendingCall;

    Drawable drawable;
    QRect texture_rect, texture_rect_portrait;
    TextureCoords texture_coords, texture_coords_portrait;
    Damage pixmapDamage;
    MTextureFromPixmap* TFP;
    bool size_needs_update;
    static MStatusBarTexture* d;
};

#endif // MSTATUSBARTEXTURE_H
