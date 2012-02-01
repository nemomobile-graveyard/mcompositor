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

#ifndef MWINDOWPROPERTYCACHE_H
#define MWINDOWPROPERTYCACHE_H

#include <QRegion>
#include <QX11Info>
#include <QVector>
#include <X11/Xutil.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xmd.h>
#include <xcb/render.h>
#include <xcb/shape.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xdamage.h>
#include <mcompatoms_p.h>

class MCSmartTimer;

/*!
 * This is a class for caching window property values for a window.
 */
class MWindowPropertyCache: public QObject
{
    Q_OBJECT
public:

    // for MCompositeManager::dumpState()
    Q_ENUMS(WindowState);
    enum WindowState {
        Unknown   = -1,
        Withdrawn = WithdrawnState,
        Iconic    = IconicState,
        Normal    = NormalState,
    };
    class Collector {
    public:
        Collector() : cookie(0), name(QLatin1String("")), requested(0) {}
        unsigned cookie;
        QLatin1String name;
        unsigned char requested;
    };
    enum CollectorKey {
        shapeRegionKey,
        customRegionKey,
        transientForKey,
        invokedByKey,
        cannotMinimizeKey,
        noAnimationsKey,
        videoOverlayKey,
        alwaysMappedKey,
        desktopViewKey,
        isDecoratorKey,
        meegoStackingLayerKey,
        lowPowerModeKey,
        opaqueWindowKey,
        prestartedAppKey,
        getWMHintsKey,
        pidKey,
        windowStateKey,
        orientationAngleKey,
        statusbarGeometryKey,
        supportedProtocolsKey,
        netWmStateKey,
        skippingTaskbarMarkerKey,
        iconGeometryKey,
        globalAlphaKey,
        videoGlobalAlphaKey,
        windowTypeAtomKey,
        realGeometryKey,
        wmNameKey,
        lastCollectorKey
    };

    /*! Construct a MWindowPropertyCache
     * \param window id to the window whose properties are cached
     *        Without one constructs a placeholder object.
     */
    MWindowPropertyCache();
    MWindowPropertyCache(Window window,
                         xcb_get_window_attributes_reply_t *attrs = 0,
                         xcb_get_geometry_reply_t *geom = 0,
                         Damage damage_obj = 0);
    virtual ~MWindowPropertyCache();

    MCompAtoms::Type windowType();

    void setRequestedGeometry(const QRect &rect) {
        req_geom = rect;
    }
    const QRect requestedGeometry() const {
        return req_geom;
    }

    // this is called on ConfigureNotify
    void setRealGeometry(const QRect &rect);

    // This is where MCompositeManager stores the expectResize().
    // Until its ConfigureNotify arrives, they are ignored.
    QRect &expectedGeometry() { return expected_geom; }

    Window winId() const { return window; }
    Window parentWindow() const { return parent_window; }
    void setParentWindow(Window w) { parent_window = w; }

    /*!
     * Returns true if we should give focus to this window.
     */
    bool wantsFocus();

    /*!
     * Returns the window group or 0.
     */
    XID windowGroup();

    /*!
     * Returns list of transients of the window.
     */
    const QList<Window>& transientWindows() const { return transients; }

    // If @state is not in _NET_WM_STATE yet, addToNetWmState() adds it
    // and returns true, otherwise just returns false.  Similarly,
    // removeFromNetWmState() only updates the property if necessary,
    // and returns whether it did.  forceSkippingTaskbar() is meant
    // to set _NET_WM_STATE_SKIP_TASKBAR temporarily, remember the
    // previous state, and restore it when the enforcement ends.
    bool addToNetWmState(Atom state);
    bool removeFromNetWmState(Atom state);
    void forceSkippingTaskbar(bool force);

    /*!
     * Returns true if this window has received MapRequest but not
     * the MapNotify yet.
     */
    bool beingMapped() const { return being_mapped; }
    void setBeingMapped(bool s) { being_mapped = s; }
    void setDontIconify(bool s) { dont_iconify = s; }
    bool dontIconify();
    bool isLockScreen();
    bool isCallUi();
    void setStackedUnmapped(bool s) { stacked_unmapped = s; }
    bool stackedUnmapped() const { return stacked_unmapped; }

    bool isMapped() const {
        if (!is_valid || !attrs)
            return false;
        return attrs->map_state == XCB_MAP_STATE_VIEWABLE;
    }

    void setIsMapped(bool s) {
        if (!is_valid || !attrs)
            return;
        // a bit ugly but avoids a round trip to X...
        if (s)
            attrs->map_state = XCB_MAP_STATE_VIEWABLE;
        else
            attrs->map_state = XCB_MAP_STATE_UNMAPPED;
    }

    void setWindowState(int state);
    
    /*!
     * Returns whether override_redirect flag was in XWindowAttributes at
     * object creation time.
     */
    bool isOverrideRedirect() const {
        if (!is_valid || !attrs)
            return false;
        return attrs->override_redirect;
    }
    bool isInputOnly() const {
        if (!is_valid || !attrs)
            return false;
        return attrs->_class == XCB_WINDOW_CLASS_INPUT_ONLY;
    }

    const xcb_get_window_attributes_reply_t* windowAttributes() const {
            return attrs; };

    void setSkippingTaskbarMarker(bool);

public slots:
    bool isDecorator();
    Atom windowTypeAtom();
    unsigned pid();

    const XWMHints &getWMHints();
    const QRect realGeometry();
    const QRectF &iconGeometry();
    const QRegion &shapeRegion();
    void shapeRefresh();

    bool hasAlpha();
    bool hasAlphaAndIsNotOpaque()
        { return hasAlpha() && !opaqueWindow(); }
    int globalAlpha();
    int videoGlobalAlpha();

    //! Returns value of TRANSIENT_FOR property.
    Window transientFor();

    //! Returns value of _MEEGOTOUCH_WM_INVOKED_BY property.
    Window invokedBy();

    //! Returns the first cardinal of WM_STATE of this window
    int windowState();

    //! Returns list of _NET_WM_STATE of the window.
    const QVector<Atom>& netWmState();

    //! Returns list of WM_PROTOCOLS of the window.
    const QList<Atom>& supportedProtocols();

    //! Returns value of _MEEGO_STACKING_LAYER. The value is between [0, 10].
    unsigned int meegoStackingLayer();

    //! Returns value of _MEEGO_LOW_POWER_MODE. The value is 0 or 1. Allows
    //  switching off compositing for any window type, and special handling
    //  when the display is off.
    unsigned int lowPowerMode();

    //! Returns value of _MEEGOTOUCH_OPAQUE_WINDOW. When 1, forces
    //  compositing off for this window even if it has an alpha channel.
    unsigned int opaqueWindow();

    //! Returns the value of _MEEGOTOUCH_ORIENTATION_ANGLE.
    unsigned orientationAngle();

    //! Returns the value of _MEEGOTOUCH_MSTATUSBAR_GEOMETRY.
    const QRect &statusbarGeometry();

    //! Returns value of _MEEGOTOUCH_ALWAYS_MAPPED.
    int alwaysMapped();

    //! Returns value of _MEEGOTOUCH_CANNOT_MINIMIZE.
    int cannotMinimize();

    //! Returns value of _MEEGOTOUCH_CUSTOM_REGION.
    const QRegion &customRegion();
    void customRegion(bool request_only);

    //! Returns value of _MEEGOTOUCH_DESKTOP_VIEW (makes sense for desktop only).
    int desktopView();
    void desktopView(bool request_only);

    // WM_NAME
    const QString &wmName();

    // Value of _MEEGOTOUCH_NO_ANIMATIONS window property. It can be used
    // for cases when restore/iconifying/closing animation is not wanted.
    unsigned int noAnimations();

    int videoOverlay();

    bool skippingTaskbarMarker();

public:
    /*!
     * Called on PropertyNotify for this window.
     * Returns true if we should re-check stacking order.
     */
    bool propertyEvent(XPropertyEvent *e);

    bool is_valid, is_virtual;

    static void set_xcb_connection(xcb_connection_t *c) {
        MWindowPropertyCache::xcb_conn = c;
    }

    /*!
     * Enables/disables damage tracking by creating/destroying the damage object.
     * Does nothing if the object is already created/destroyed.
     * The damage report level is XDamageReportNonEmpty.
     */
    void damageTracking(bool enabled);

    /*!
     * Enables damage tracking by creating a damage object with the given report
     * level. If an object with the given level already exists nothing happens,
     * if an object with another level exists it is destroyed and a new one is
     * created.
     */
    void damageTracking(int damageReportLevel);
    // XDamageSubtract wrapper for unit testing
    void damageSubtract();
    // for unit testing of damage handling code
    void damageReceived();

    /*! 
     * Returns whether this is an application window
     */
    bool isAppWindow(bool include_transients = false);

    /*! 
     * Reads and breaks down the _MEEGO_SPLASH_SCREEN window property
     * of @win.
     */
    static bool readSplashProperty(Window win,
                   unsigned &splash_pid, unsigned &splash_pixmap,
                   QString &splash_landscape, QString &splash_portrait);

    /*! 
     * Is this a special property cache without a corresponding X window?
     * "virtual is invalid but in a happy way" -- Kimmo
     */
    bool isVirtual() const { return is_virtual; }

    int waitingForDamage() const;
    void setWaitingForDamage(int waiting);
signals:
    void iconGeometryUpdated();
    void desktopViewChanged(MWindowPropertyCache *pc);
    void alwaysMappedChanged(MWindowPropertyCache *pc);
    void customRegionChanged(MWindowPropertyCache *pc);

private slots:
    bool prestartedApp();

private:
    void init();
    void init_invalid();
    bool getCARD32(const CollectorKey key, CARD32 *value);

protected:
    Window transient_for;
    Window invoked_by;
    QList<Window> transients;
    QList<Atom> wm_protocols;
    QRectF icon_geometry;
    signed char has_alpha;
    int global_alpha;
    int video_global_alpha;
    bool is_decorator;
    QVector<Atom> net_wm_state;
    // @force_skipping_taskbar indicates whether forceSkippingTaskbar()
    // is in effect, and whether @was_skipping_taskbar is to be restored
    bool force_skipping_taskbar, was_skipping_taskbar;
    // geometry is requested only once in the beginning, after that, we
    // use ConfigureNotifys to update the size through setRealGeometry()
    QRect req_geom, real_geom, expected_geom, statusbar_geom;
    XWMHints *wmhints;
    xcb_get_window_attributes_reply_t *attrs;
    unsigned meego_layer, low_power_mode, opaque_window;
    bool prestarted;
    int window_state;
    QVector<Atom> type_atoms;
    MCompAtoms::Type window_type;
    Window window, parent_window;
    int always_mapped, cannot_minimize, desktop_view;
    bool being_mapped, dont_iconify, stacked_unmapped;
    QRegion custom_region;
    unsigned orientation_angle;
    QRegion shape_region;

    // @requests stores the state of the property requests, indexed by
    // the SLOT() of the collector function (isDecorator(), customRegion(),
    // etc).  If the collector is not in the hash then the property value
    // has not requested yet.  If the value is non-zero a request is ongoing.
    // Otherwise if the value is zero the property value is considered up
    // to date.
    //
    // When the object is initialized we request the values of some
    // properties.  When a property value we're interested in changes
    // we cancel any ongoing requests about that property and make a
    // new one.  On the destruction of the object we cancel all requests.
    //
    // When a collector function is called and it doesn't find itself in
    // the @requests table it makes a requesgts and waits for the reply.
    // If it see that a request has been ongoing it just waits for the
    // reply.  Otherwise, if it finds that the property value is known
    // and has not been changed it simply returns it.  If the property
    // cache object is not valid then it just returns the default value
    // set by init().
    //
    // When a request is made @collect_timer is restarted, and we collect
    // the reply unconditionally when it expires.
    MCSmartTimer *collect_timer;
    Collector requests[lastCollectorKey];
    bool isUpdate(const CollectorKey collector);
    void addRequest(const CollectorKey key, const QLatin1String &collector,
                    unsigned cookie);
    void replyCollected(const CollectorKey key);
    void cancelRequest(const CollectorKey key);
    unsigned requestProperty(Atom prop, Atom type, unsigned n = 1);

    // Overloads to make the routines above callable with other types.
    void addRequest(const CollectorKey key, const char *collector, unsigned cookie)
        { addRequest(key, QLatin1String(collector), cookie); }
    unsigned requestProperty(MCompAtoms::Atoms prop, Atom type,
                             unsigned n = 1)
        { return requestProperty(MCompAtoms::atoms[prop], type, n); }
    // some unit tests want to fake window properties
    void cancelAllRequests();

    static xcb_connection_t *xcb_conn;
    static xcb_render_query_pict_formats_reply_t *pict_formats_reply;
    static xcb_render_query_pict_formats_cookie_t pict_formats_cookie;
    Damage damage_object;
    int damage_report_level;
    int waiting_for_damage;
    QString wm_name;
    unsigned wm_pid, no_animations;
    int video_overlay;
    bool pending_damage;
    bool skipping_taskbar_marker;

    friend class ut_Compositing;
};

// Non-deletable dummy MWindowPropertyCache.
class MWindowDummyPropertyCache: public MWindowPropertyCache
{
public:
    static MWindowDummyPropertyCache *get();

private:
    virtual bool event(QEvent *e);

    static MWindowDummyPropertyCache *singleton;
};

#endif
