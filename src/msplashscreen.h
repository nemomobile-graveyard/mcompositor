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

#ifndef MSPLASHSCREEN_H
#define MSPLASHSCREEN_H

#include <mtexturepixmapitem.h>
#include <mwindowpropertycache.h>

// Non-deletable, static MWindowPropertyCache.
class MSplashPropertyCache: public MWindowPropertyCache
{
public:
    MSplashPropertyCache();
    static MSplashPropertyCache *get();
    void setOrientationAngle(int angle);
    void setPid(unsigned pid);

private:
    bool event(QEvent *e);

    xcb_get_window_attributes_reply_t attrs;
};

/*!
 * Class for splash screens, which look like normal window objects but don't
 * refresh their pixmap and don't have a window.
 */
class MSplashScreen: public MTexturePixmapItem
{
    Q_OBJECT

public:
    enum { Type = UserType + 3 };
    int type() const { return Type; }

    MSplashScreen(unsigned int pid,
                  const QString &splash_p, const QString &splash_l,
                  unsigned int pixmap);

    ~MSplashScreen();

    Qt::HANDLE window() const { return win_id; }
    Pixmap windowPixmap() const { return pixmap; }
    QRectF boundingRect() const;
    bool matches(MWindowPropertyCache *pc) const;
    bool same(unsigned pid, const QString &splash_p,
              const QString &splash_l, unsigned pixmap) const;

    void endAnimation();
    void beginAnimation();

private slots:
    void iconified();

private:
    QString wm_class;
    QString portrait_file;
    QString landscape_file;
    unsigned int pixmap;
    QPixmap *q_pixmap;
    QTimer timer;
    bool fade_animation;
    Window win_id;

    friend class ut_splashscreen;
};

#endif
