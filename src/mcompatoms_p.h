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

#ifndef MCOMPATOMS_P_H
#define MCOMPATOMS_P_H

#include <QObject>
#include <X11/Xutil.h>

uint qHash(const QLatin1String &key);

// this class is not instantiated, we only use the metaobject
class MCompAtoms: public QObject
{
    Q_OBJECT
public:
      static void init();

    // note that this enum is ordered and presents
    // the depth ordering of different window types
    Q_ENUMS(Type);
    enum Type {
        INVALID = 0,
        DESKTOP,
        NORMAL,
        DIALOG,
        SHEET,
        NO_DECOR_DIALOG,
        FRAMELESS,
        DOCK,
        INPUT,
        ABOVE,
        NOTIFICATION,
        DECORATOR,
        UNKNOWN
    };

    Q_ENUMS(Atoms);
    enum Atoms {
        // The following atoms are added to the _NET_SUPPORTED list.
        // window manager
        WM_PROTOCOLS,
        WM_DELETE_WINDOW,
        WM_TAKE_FOCUS,
        WM_TRANSIENT_FOR,
        WM_HINTS,

        // window types
        _NET_SUPPORTED,
        _NET_SUPPORTING_WM_CHECK,
        _NET_WM_NAME,
        _NET_WM_WINDOW_TYPE,
        _NET_WM_WINDOW_TYPE_DESKTOP,
        _NET_WM_WINDOW_TYPE_NORMAL,
        _NET_WM_WINDOW_TYPE_DOCK,
        _NET_WM_WINDOW_TYPE_INPUT,
        _NET_WM_WINDOW_TYPE_NOTIFICATION,
        _NET_WM_WINDOW_TYPE_DIALOG,
        _NET_WM_WINDOW_TYPE_MENU,
        _NET_WM_STATE_ABOVE,
        _NET_WM_STATE_SKIP_TASKBAR,
        _NET_WM_STATE_FULLSCREEN,
        _NET_WM_STATE_MODAL,
        _KDE_NET_WM_WINDOW_TYPE_OVERRIDE,

        // window properties
        _NET_WM_WINDOW_OPACITY,
        _NET_WM_STATE,
        _NET_WM_ICON_GEOMETRY,
        _NET_WM_USER_TIME_WINDOW,
        WM_STATE,
        WM_NAME,
        WM_CLASS,

        // misc
        _NET_WM_PID,
        _NET_WM_PING,

        // root messages
        _NET_ACTIVE_WINDOW,
        _NET_CLOSE_WINDOW,
        _NET_CLIENT_LIST,
        _NET_CLIENT_LIST_STACKING,
        WM_CHANGE_STATE,

        // MEEGO(TOUCH)-specific
        _MEEGOTOUCH_DECORATOR_WINDOW,
        _MEEGOTOUCH_GLOBAL_ALPHA,
        _MEEGOTOUCH_VIDEO_ALPHA,
        _MEEGO_STACKING_LAYER,
        _MEEGOTOUCH_CURRENT_APP_WINDOW,
        _MEEGOTOUCH_ALWAYS_MAPPED,
        _MEEGOTOUCH_DESKTOP_VIEW,
        _MEEGOTOUCH_CANNOT_MINIMIZE,
        _MEEGOTOUCH_MSTATUSBAR_GEOMETRY,
        _MEEGOTOUCH_CUSTOM_REGION,
        _MEEGOTOUCH_ORIENTATION_ANGLE,
        _MEEGOTOUCH_NET_WM_WINDOW_TYPE_SHEET,
        _MEEGOTOUCH_WM_INVOKED_BY,
        _MEEGO_SPLASH_SCREEN,
        _MEEGO_LOW_POWER_MODE,
        _MEEGOTOUCH_OPAQUE_WINDOW,
        _MEEGOTOUCH_PRESTARTED,
        _MEEGOTOUCH_STATUSBAR_VISIBLE,
        _MEEGOTOUCH_NO_ANIMATIONS,
        _MCOMPOSITOR_SKIP_TASKBAR,

        // set to 1 if there is a video overlay
        _OMAP_VIDEO_OVERLAY,

#ifdef WINDOW_DEBUG
        _M_WM_INFO,
        _M_WM_WINDOW_ZVALUE,
        _M_WM_WINDOW_COMPOSITED_VISIBLE,
        _M_WM_WINDOW_COMPOSITED_INVISIBLE,
        _M_WM_WINDOW_DIRECT_VISIBLE,
        _M_WM_WINDOW_DIRECT_INVISIBLE,
#endif

        ATOMS_TOTAL
    };

    static Atom atoms[ATOMS_TOTAL];

    // RROutput properties
    static union randr_t {
        struct __attribute__((packed)) {
            Atom ctype, panel, alpha_mode, graphics_alpha, video_alpha;
        };
        Atom atoms[];
    } randr;
};

#define ATOM(t) MCompAtoms::atoms[MCompAtoms::t]

#endif
