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

#ifndef DUICOMPOSITEMANAGER_P_H
#define DUICOMPOSITEMANAGER_P_H

#include "mcontextproviderwrapper.h"

#include <QObject>
#include <QHash>
#include <QPixmap>
#include <QTimer>
#include <QDir>
#include <QPointer>
#include <QSocketNotifier>
#include <QElapsedTimer>

#include <X11/Xutil.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xdamage.h>
#include <X11/Xlib-xcb.h>

#include "mrestacker.h"

class QGraphicsScene;
class QGLWidget;

class MCompositeManager;
class MCompositeScene;
class MSimpleWindowFrame;
class MCompAtoms;
class MCompositeWindow;
class MDeviceState;
class MWindowPropertyCache;
class MCompositeManagerExtension;
class MSplashScreen;

/*!
 * Internal implementation of MCompositeManager
 */

class MCompositeManagerPrivate: public QObject
{
    Q_OBJECT
public:

    MCompositeManagerPrivate(MCompositeManager *p);
    ~MCompositeManagerPrivate();

    MCompositeWindow *bindWindow(Window w, bool startup = false);
    QGraphicsScene *scene();

    MWindowPropertyCache* getPropertyCache(Window window,
                         xcb_get_window_attributes_reply_t *attrs = 0,
                         xcb_get_geometry_reply_t *geom = 0,
                         Damage damage_obj = 0);
    void prepare();
    void loadPlugin(const QString &fileName);
    int loadPlugins(const QDir &dir);
    void activateWindow(Window w, Time timestamp,
		        bool disableCompositing = true,
                        bool stacked = false);
    bool updateNetClientList(Window w, bool addit);
    void setWindowState(Window, int, int level = 0);
#ifdef WINDOW_DEBUG
    void setWindowDebugProperties(Window w);
#endif
    void iconifyApps();
    void positionWindow(Window w, bool on_top);
    void addItem(MCompositeWindow *item);
    void damageEvent(XDamageNotifyEvent *);
    void destroyEvent(XDestroyWindowEvent *);
    void propertyEvent(XPropertyEvent *);
    void unmapEvent(XUnmapEvent *);
    void configureEvent(XConfigureEvent *, bool nostacking = false);
    void configureRequestEvent(XConfigureRequestEvent *);
    void mapEvent(XMapEvent *e, bool startup = false);
    void mapRequestEvent(XMapRequestEvent *);
    void rootMessageEvent(XClientMessageEvent *);
    void clientMessageEvent(XClientMessageEvent *);
    void keyEvent(XKeyEvent*);
    void installX11EventFilter(long xevent, MCompositeManagerExtension* extension);
    
    void redirectWindows();
    void showOverlayWindow(bool show);
    void enableRedirection();
    void setExposeDesktop(bool exposed);
    void fixZValues();
    void setStatusbarVisibleProperty(bool visiblity);
    void checkStacking(bool force_visibility_check,
                       Time timestamp = CurrentTime);
    void checkInputFocus(Time timestamp = CurrentTime);
    void configureWindow(MWindowPropertyCache *pc, XConfigureRequestEvent *e);

    Window getTopmostApp(int *index_in_stacking_list = 0,
                         Window ignore_window = 0,
                         bool skip_always_mapped = false);
    Window getLastVisibleParent(MWindowPropertyCache *pc);
    int indexOfLastVisibleWindow() const;
    bool hasTransientVKB(MWindowPropertyCache *pc) const;

    bool possiblyUnredirectTopmostWindow();
    bool haveMappedWindow() const;
    bool x11EventFilter(XEvent *event, bool startup = false);
    bool processX11EventFilters(XEvent *event, bool after);
    void removeWindow(Window w);
    bool needDecoration(MWindowPropertyCache *pc);
    bool skipStartupAnim(MWindowPropertyCache *pc, bool iconic_is_ok = false);
    MCompositeWindow *getHighestDecorated(int *index = 0);
    void setWindowStateForTransients(MWindowPropertyCache *pc, int state,
                                     int level = 0);
    
    void roughSort();
    void setCurrentApp(MCompositeWindow *cw, bool restacked);
    bool raiseWithTransients(MWindowPropertyCache *pc,
                           int parent_idx, QList<int> *anewpos = NULL);
    MWindowPropertyCache *findLockScreen() const;

    MCompositeScene *watch;
    Window localwin, localwin_parent, wm_window;
    Window xoverlay;
    Window prev_focus;
    Window current_app;

    QGLWidget *glwidget;

    // @stacking_list:  The stacking to be effected by checkStacking(),
    //                  ie. how we'll restack the next time around.
    // @netClientList:  Mirrors the root window's _NET_CLIENT_LIST,
    //                  and contains most of our managed, mapped windows
    //                  in the order they have been mapped.  It is always
    //                  kept up-to-date and is alteres exclusively by
    //                  updateNetClientList().
    // @prevNetClientListStacking:
    //                  The most recently set _NET_CLIENT_LIST_STACKING.
    //                  The property value is generated from @stacking_list,
    //                  and this list is only used to see whether it needs
    //                  to be updated.
    // @xserver_stacking: Our most current understanding of the toplevel
    //                  toplevel windows (every children of the root).
    //                  It is loaded once when we start up, then kept up
    //                  to date as we receive X window events.
    Window desktop_window;
    QList<Window> stacking_list;
    QVector<Window> netClientList;
    QVector<Window> prevNetClientListStacking;
    MRestacker xserver_stacking;

    // These maps contain all known objects, indexed by the window XID
    // they belong to.  When an X window is destroyed its objects are
    // deleteLater()ed and removed from these maps.
    QHash<Window, MCompositeWindow *> windows;
    QHash<Window, MWindowPropertyCache*> prop_caches;

#ifdef ENABLE_BROKEN_SIMPLEWINDOWFRAME
    struct FrameData {
        FrameData(): frame(0), parentWindow(0), mapped(false) {}
        MSimpleWindowFrame *frame;
        Window                parentWindow;
        bool mapped;
    };
    QHash<Window, FrameData> framed_windows;
#endif

    // X window event => extension mapping; describes which extensions
    // handle a particular event.
    QMultiHash<int, MCompositeManagerExtension* > m_extensions;

    int damage_event;
    int damage_error;

    bool compositing;
    bool overlay_mapped;
    bool changed_properties;
    MDeviceState *device_state;
    MContextProviderWrapper orientationProvider;

    // Indicates whether MCompositeManager::prepare() has finished.
    // Used by the destructor.
    bool prepared;

    // Tells whether invocation of "Application hung, close it?" dialogs
    // was disabled by a command line switch.
    bool mayShowApplicationHungDialog;

    xcb_connection_t *xcb_conn;

    // mechanism for lazy stacking
    QTimer stacking_timer;
    bool stacking_timeout_check_visibility;
    Time stacking_timeout_timestamp;
    void dirtyStacking(bool force_visibility_check, Time t = CurrentTime);
    void pingTopmost();
    MSplashScreen *splash;
    QPointer<MCompositeWindow> waiting_damage;
    QTimer lockscreen_map_timer;
    QSocketNotifier *sighupNotifier;

    struct DismissedSplash {
        // time in ms to block activation of a window after we have seen it for
        // the first time
        static const int BLOCK_DURATION = 1000;

        DismissedSplash() {
            blockTimer.invalidate();
            lifetimeTimer.start();
        }

        // Is the window supposed to be/stay iconified?
        bool isBlocking() {
            return (!blockTimer.isValid()) || blockTimer.elapsed() < BLOCK_DURATION;
        }

        QElapsedTimer blockTimer;
        QElapsedTimer lifetimeTimer;
    };
    QHash<unsigned int, DismissedSplash> dismissedSplashScreens;

    // When splashTimeout() is called the information about a splash screen is
    // removed from the property cache, the window and stacking list.
    // As positionWindow() might be called with a splash screen window as
    // parameter after the timeout, the information about the last splash screen
    // has to be saved.
    struct DestroyedSplash {
        DestroyedSplash(Window window, unsigned int pid)
            : window(window), pid(pid) {
        }

        Window window;
        unsigned int pid;
    } lastDestroyedSplash;

    int defaultGraphicsAlpha;
    int defaultVideoAlpha;
    bool globalAlphaOverridden;

signals:
    void currentAppChanged(Window w);
    void windowBound(MCompositeWindow* window);

public slots:

    void sendSyntheticVisibilityEventsForOurBabies();
    void gotHungWindow(MCompositeWindow *window, bool is_hung);
    void enableCompositing();

    void lowerHandler(MCompositeWindow *window);
    void restoreHandler(MCompositeWindow *window);
    void closeHandler(MCompositeWindow *window);
    
    void onFirstAnimationStarted();
    void onAnimationsFinished(MCompositeWindow*);
    void exposeSwitcher();
    
    void displayOff(bool display_off);
    void callOngoing(bool call_ongoing);
    void stackingTimeout();
    void splashTimeout();
};

#endif
