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

#ifndef DUICOMPOSITEMANAGER_P_H
#define DUICOMPOSITEMANAGER_P_H

#include <QObject>
#include <QHash>
#include <QPixmap>

#include <X11/Xutil.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xdamage.h>

class QGraphicsScene;
class QGLWidget;

class DuiCompositeScene;
class DuiSimpleWindowFrame;
class DuiCompAtoms;
class DuiCompositeWindow;

enum {
    INPUT_LAYER = 0,
    DOCK_LAYER = 0,
    SYSTEM_LAYER,
    APPLICATION_LAYER,
    DESKTOP_LAYER,
    TOTAL_LAYERS
};

/*!
 * Internal implementation of DuiCompositeManager
 */

class DuiCompositeManagerPrivate: public QObject
{
    Q_OBJECT
public:
    enum StackPosition {
        STACK_BOTTOM = 0,
        STACK_TOP
    };

    DuiCompositeManagerPrivate(QObject *p);
    ~DuiCompositeManagerPrivate();

    static Window parentWindow(Window child);
    DuiCompositeWindow *texturePixmapItem(Window w);
    DuiCompositeWindow *bindWindow(Window win, DuiCompositeWindow *transient = 0);
    QGraphicsScene *scene();

    void prepare();
    void activateWindow(Window w);
    //! Update the _NET_CLIENT_LIST and _NET_CLIENT_LIST_STACKING
    void updateWinList(bool stackingOnly = false);
    void setWindowState(Window , int);

    void topmostWindowsRaise();
    void positionWindow(Window w, StackPosition pos);
    void addItem(DuiCompositeWindow *item);
    void damageEvent(XDamageNotifyEvent *);
    void destroyEvent(XDestroyWindowEvent *);
    void propertyEvent(XPropertyEvent *);
    void unmapEvent(XUnmapEvent *);
    void configureEvent(XConfigureEvent *);
    void configureRequestEvent(XConfigureRequestEvent *);
    void mapEvent(XMapEvent *);
    void mapRequestEvent(XMapRequestEvent *);
    void rootMessageEvent(XClientMessageEvent *);
    void clientMessageEvent(XClientMessageEvent *);
    void redirectWindows();
    void mapOverlayWindow();
    //! Redirects existing items
    void enableRedirection();
    void exposeDesktop(bool exposed);

    bool isRedirected(Window window);
    bool x11EventFilter(XEvent *event);
    bool removeWindow(Window w);
    bool isSelfManagedFocus(Window w);
    bool needDecoration(Window w);

    DuiCompositeScene *watch;
    Window localwin;
    Window xoverlay;
    Window prev_focus;
    Window iconify_window;
    Window raise_window;

    static Window stack[TOTAL_LAYERS];

    DuiCompAtoms *atom;
    QGLWidget *glwidget;
    DuiCompositeWindow *damage_cache;

    QList<Window> stacking_list;
    QHash<Window, DuiCompositeWindow *> windows;
    struct FrameData {
        FrameData(): frame(0), parentWindow(0), mapped(false) {}
        DuiSimpleWindowFrame *frame;
        Window                parentWindow;
        bool mapped;
    };
    QHash<Window, FrameData> framed_windows;
    QRegion dock_region;

    int damage_event;
    int damage_error;

    bool arranged;
    bool compositing;
    bool got_active_window;

signals:
    void inputEnabled();
    void compositingEnabled();

public slots:

    void gotHungWindow(DuiCompositeWindow *window);
    void sendPing(DuiCompositeWindow *window);

    void enableInput();
    void disableInput();
    void enableCompositing();
    void disableCompositing();
    void showLaunchIndicator(int timeout);
    void hideLaunchIndicator();
    void iconifyOnComposite();
    void raiseOnComposite();
    void exposeDesktop();
    void directRenderDesktop();
};

#endif
