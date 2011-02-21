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
#include <QHash>
#include <QVector>
#include <X11/Xutil.h>
#include <X11/Xlib-xcb.h>
#include <xcb/render.h>
#include <xcb/shape.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xdamage.h>
#include "mcompatoms_p.h"

class MCSmartTimer;

/*!
 * This is a class for caching window property values for a window.
 */
class MWindowPropertyCache: public QObject
{
    Q_OBJECT
public:

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

    // used to set the atom list now, for immediate effect in e.g. stacking
    void setNetWmState(const QList<Atom>& s);

    /*!
     * Returns true if this window has received MapRequest but not
     * the MapNotify yet.
     */
    bool beingMapped() const { return being_mapped; }
    void setBeingMapped(bool s) { being_mapped = s; }
    void setDontIconify(bool s) { dont_iconify = s; }
    bool dontIconify();

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

    const QRect &homeButtonGeometry();
    const QRect &closeButtonGeometry();

public slots:
    bool isDecorator();
    Atom windowTypeAtom();

    const XWMHints &getWMHints();
    const QRect realGeometry();
    const QRectF &iconGeometry();
    const QRegion &shapeRegion();
    void shapeRefresh();

    bool hasAlpha();
    int globalAlpha();
    int videoGlobalAlpha();

    //! Returns value of TRANSIENT_FOR property.
    Window transientFor();

    //! Returns the first cardinal of WM_STATE of this window
    int windowState();

    //! Returns list of _NET_WM_STATE of the window.
    const QList<Atom>& netWmState();

    //! Returns list of WM_PROTOCOLS of the window.
    const QList<Atom>& supportedProtocols();

    //! Returns value of _MEEGO_STACKING_LAYER. The value is between [0, 6].
    unsigned int meegoStackingLayer();

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

public:
    /*!
     * Called on PropertyNotify for this window.
     * Returns true if we should re-check stacking order.
     */
    bool propertyEvent(XPropertyEvent *e);

    bool is_valid;

    static void set_xcb_connection(xcb_connection_t *c) {
        MWindowPropertyCache::xcb_conn = c;
    }

    void damageTracking(bool enabled)
    {
        if (!is_valid || (damage_object && enabled))
            return;
        if (damage_object && !enabled) {
            XDamageDestroy(QX11Info::display(), damage_object);
            damage_object = 0;
        }
        else if (enabled && !damage_object && !isInputOnly())
            damage_object = XDamageCreate(QX11Info::display(), window,
                                          XDamageReportNonEmpty); 
    }

signals:
    void iconGeometryUpdated();
    void meegoDecoratorButtonsChanged(Window w);
    void desktopViewChanged(MWindowPropertyCache *pc);
    void alwaysMappedChanged(MWindowPropertyCache *pc);
    void customRegionChanged(MWindowPropertyCache *pc);

private slots:
    void buttonGeometryHelper();

private:
    void init();
    void init_invalid();
    int alphaValue(const QLatin1String me);

    Window transient_for;
    QList<Window> transients;
    QList<Atom> wm_protocols;
    QRectF icon_geometry;
    signed char has_alpha;
    int global_alpha;
    int video_global_alpha;
    bool is_decorator;
    QList<Atom> net_wm_state;
    // geometry is requested only once in the beginning, after that, we
    // use ConfigureNotifys to update the size through setRealGeometry()
    QRect req_geom, real_geom, statusbar_geom;
    QRect home_button_geom, close_button_geom;
    XWMHints *wmhints;
    xcb_get_window_attributes_reply_t *attrs;
    unsigned meego_layer;
    int window_state;
    QVector<Atom> type_atoms;
    MCompAtoms::Type window_type;
    Window window, parent_window;
    int always_mapped, cannot_minimize, desktop_view;
    bool being_mapped, dont_iconify;
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
    QHash<const QLatin1String, unsigned> requests;
    bool isUpdate(const QLatin1String collector);
    bool requestPending(const QLatin1String collector);
    void addRequest(const QLatin1String collector, unsigned cookie);
    void replyCollected(const QLatin1String collector);
    void cancelRequest(const QLatin1String collector);
    unsigned requestProperty(Atom prop, Atom type, unsigned n = 1);

    // Overloads to make the routines above callable with other types.
    bool isUpdate(const char *collector)
        { return isUpdate(QLatin1String(collector)); }
    bool requestPending(const char *collector)
        { return requestPending(QLatin1String(collector)); }
    void addRequest(const char *collector, unsigned cookie)
        { addRequest(QLatin1String(collector), cookie); }
    void replyCollected(const char *collector)
        { replyCollected(QLatin1String(collector)); }
    void cancelRequest(const char *collector)
        { cancelRequest(QLatin1String(collector)); }
    unsigned requestProperty(MCompAtoms::Atoms prop, Atom type,
                             unsigned n = 1)
        { return requestProperty(MCompAtoms::instance()->getAtom(prop),
                                 type, n); }

    static xcb_connection_t *xcb_conn;
    static xcb_render_query_pict_formats_reply_t *pict_formats_reply;
    static xcb_render_query_pict_formats_cookie_t pict_formats_cookie;
    Damage damage_object;
    QString wm_name;
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
