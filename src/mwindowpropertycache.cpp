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

#include <QtGui>
#include <stdlib.h>
#include <QX11Info>
#include <QRect>
#include <QDebug>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>
#include <X11/Xmd.h>
#include "mcompositemanager.h"
#include "mwindowpropertycache.h"
#include "mcompositemanager_p.h"

#define MAX_TYPES 10

// Simple derivate of QTimer to stop itself when nobody is connected to
// timeout().  Used for MWindowPropertyCache::collect_timer to avoid
// unnecessary wakeups.
class MCSmartTimer: public QTimer
{
public:
    MCSmartTimer(QObject *parent): QTimer(parent) { }
    void disconnect(const char *slot)
    {
      QObject::disconnect(this, SIGNAL(timeout()), parent(), slot);
        if (!receivers(SIGNAL(timeout())))
            stop();
    }
};

xcb_render_query_pict_formats_reply_t *MWindowPropertyCache::pict_formats_reply = 0;
xcb_render_query_pict_formats_cookie_t MWindowPropertyCache::pict_formats_cookie = {0};

// Returns whether the property of @collector does not need to be refreshed:
// if it has been requested and it has been replied.
bool MWindowPropertyCache::isUpdate(const QLatin1String collector)
{
    return requests.contains(collector) && !requests[collector];
}

// Returns whether @collector's property is being queried:
// if it has been requested but hasn't been replied.
bool MWindowPropertyCache::requestPending(const QLatin1String collector)
{
    return requests.contains(collector) &&  requests[collector];
}

// Called when @collector's property is being queried, and it sets up
// a timer to collect the reply in a while.  If a query is already ongoing
// it's cancelled.  @cookie should be what xcb_*() returned.
void MWindowPropertyCache::addRequest(const QLatin1String collector,
                                      unsigned cookie)
{
    unsigned prev_cookie = requests[collector];
    requests[collector] = cookie;
    if (prev_cookie)
        xcb_discard_reply(xcb_conn, prev_cookie);
    else
        connect(collect_timer, SIGNAL(timeout()), this, collector.latin1());
    collect_timer->start();
}

// Makes @collector's property considered isUpdate().
void MWindowPropertyCache::replyCollected(const QLatin1String collector)
{
    requests[collector] = 0;
    collect_timer->disconnect(collector.latin1());
}

// If @collector has an ongoing query, cancels it.  @collector's property
// will have been considered isUpdate().
void MWindowPropertyCache::cancelRequest(const QLatin1String collector)
{
    if (requestPending(collector)) {
        xcb_discard_reply(xcb_conn, requests[collector]);
        replyCollected(collector);
    } else
        requests[collector] = 0;
}

// Shorthand to request the value of a property.  Returns what you can
// pass to addRequest().
unsigned MWindowPropertyCache::requestProperty(Atom prop, Atom type,
                                               unsigned n)
{
    return xcb_get_property(xcb_conn, 0, window,
                            prop, type, 0, n).sequence;
}

xcb_connection_t *MWindowPropertyCache::xcb_conn;

void MWindowPropertyCache::init()
{
    transient_for = None,
    has_alpha = -1;
    global_alpha = 255;
    video_global_alpha = -1;
    is_decorator = false;
    wmhints = XAllocWMHints();
    attrs = 0;
    meego_layer = 0;
    window_state = -1;
    window_type = MCompAtoms::INVALID;
    parent_window = QX11Info::appRootWindow();
    always_mapped = 0;
    cannot_minimize = 0;
    desktop_view = -1;
    being_mapped = false;
    dont_iconify = false;
    orientation_angle = 0;
    damage_object = 0;
    collect_timer = 0;
}

void MWindowPropertyCache::init_invalid()
{
    is_valid = false;
}

MWindowPropertyCache::MWindowPropertyCache(Window w,
                        xcb_get_window_attributes_reply_t *wa,
                        xcb_get_geometry_reply_t *geom,
                        Damage damage_obj)
    : window(w)
{
    init();
    if (!wa) {
        attrs = xcb_get_window_attributes_reply(xcb_conn,
                        xcb_get_window_attributes(xcb_conn, window), 0);
        if (!attrs) {
            //qWarning("%s: invalid window 0x%lx", __func__, window);
            init_invalid();
            if (damage_obj)
                XDamageDestroy(QX11Info::display(), damage_obj);
            return;
        }
    } else
        attrs = wa;

    is_valid = true;
    damage_object = damage_obj;

    if (!isMapped()) {
        // required to get property changes happening before mapping
        // (after mapping, MCompositeManager sets the window's input mask)
        XSelectInput(QX11Info::display(), window, PropertyChangeMask);
        XShapeSelectInput(QX11Info::display(), window, ShapeNotifyMask);
    }

    collect_timer = new MCSmartTimer(this);
    collect_timer->setInterval(5000);
    collect_timer->setSingleShot(true);

    if (geom) {
        real_geom = QRect(geom->x, geom->y, geom->width, geom->height);
        requests[QLatin1String(SLOT(realGeometry()))] = 0;
    } else
        addRequest(SLOT(realGeometry()),
                   xcb_get_geometry(xcb_conn, window).sequence);
    addRequest(SLOT(isDecorator()), 
               requestProperty(MCompAtoms::_MEEGOTOUCH_DECORATOR_WINDOW,
                               XCB_ATOM_CARDINAL));
    addRequest(SLOT(transientFor()),
               requestProperty(XCB_ATOM_WM_TRANSIENT_FOR,
                               XCB_ATOM_WINDOW));
    addRequest(SLOT(meegoStackingLayer()),
               requestProperty(MCompAtoms::_MEEGO_STACKING_LAYER,
                               XCB_ATOM_CARDINAL));
    addRequest(SLOT(windowTypeAtom()),
               requestProperty(MCompAtoms::_NET_WM_WINDOW_TYPE,
                               XCB_ATOM_ATOM, MAX_TYPES));
    if (!pict_formats_reply && !pict_formats_cookie.sequence)
        pict_formats_cookie = xcb_render_query_pict_formats(xcb_conn);
    addRequest(SLOT(buttonGeometryHelper()),
               requestProperty(MCompAtoms::_MEEGOTOUCH_DECORATOR_BUTTONS,
                               XCB_ATOM_CARDINAL, 8));
    addRequest(SLOT(orientationAngle()),
               requestProperty(MCompAtoms::_MEEGOTOUCH_ORIENTATION_ANGLE,
                               XCB_ATOM_CARDINAL));
    addRequest(SLOT(statusbarGeometry()),
               requestProperty(MCompAtoms::_MEEGOTOUCH_MSTATUSBAR_GEOMETRY,
                               XCB_ATOM_CARDINAL, 4));
    addRequest(SLOT(supportedProtocols()),
               requestProperty(MCompAtoms::WM_PROTOCOLS,
                               XCB_ATOM_ATOM, 100));
    addRequest(SLOT(windowState()),
               requestProperty(MCompAtoms::WM_STATE, ATOM(WM_STATE)));
    addRequest(SLOT(getWMHints()),
               requestProperty(XCB_ATOM_WM_HINTS, XCB_ATOM_WM_HINTS, 10));
    addRequest(SLOT(iconGeometry()),
               requestProperty(MCompAtoms::_NET_WM_ICON_GEOMETRY,
                               XCB_ATOM_CARDINAL, 4));
    addRequest(SLOT(globalAlpha()),
               requestProperty(MCompAtoms::_MEEGOTOUCH_GLOBAL_ALPHA,
                                XCB_ATOM_CARDINAL));
    addRequest(SLOT(videoGlobalAlpha()),
               requestProperty(MCompAtoms::_MEEGOTOUCH_VIDEO_ALPHA,
                                XCB_ATOM_CARDINAL));
    if (!isInputOnly())
        addRequest(SLOT(shapeRegion()),
                   xcb_shape_get_rectangles(xcb_conn, window,
                                            ShapeBounding).sequence);
    addRequest(SLOT(netWmState()),
               requestProperty(MCompAtoms::_NET_WM_STATE,
                               XCB_ATOM_ATOM, 100));
    addRequest(SLOT(alwaysMapped()),
               requestProperty(MCompAtoms::_MEEGOTOUCH_ALWAYS_MAPPED,
                                XCB_ATOM_CARDINAL));
    addRequest(SLOT(cannotMinimize()),
               requestProperty(MCompAtoms::_MEEGOTOUCH_CANNOT_MINIMIZE,
                                XCB_ATOM_CARDINAL));
    addRequest(SLOT(wmName()),
               requestProperty(MCompAtoms::WM_NAME, XCB_ATOM_STRING, 100));

    // add any transients to the transients list
    MCompositeManager *m = (MCompositeManager*)qApp;
    for (QList<Window>::const_iterator it = m->d->stacking_list.begin();
         it != m->d->stacking_list.end(); ++it) {
        MWindowPropertyCache *p = m->d->prop_caches.value(*it, 0);
        if (p && p != this && p->transientFor() == window)
            transients.append(*it);
    }
    connect(this, SIGNAL(meegoDecoratorButtonsChanged(Window)),
            m->d, SLOT(setupButtonWindows(Window)));
}

MWindowPropertyCache::MWindowPropertyCache()
    : window(None)
{
    init();
    init_invalid();
}

MWindowPropertyCache::~MWindowPropertyCache()
{
    if (!is_valid) {
        // no pending XCB requests
        XFree(wmhints);
        return;
    }

    if (transient_for && transient_for != (Window)-1) {
        MCompositeManager *m = (MCompositeManager*)qApp;
        // remove reference from the old "parent"
        MWindowPropertyCache *p = m->d->prop_caches.value(transient_for, 0);
        if (p) p->transients.removeAll(window);
    }

    // Discard pending replies.
    for (QHash<const QLatin1String, unsigned>::const_iterator i = requests.begin();
         i != requests.end(); ++i)
      if (i.value())
          xcb_discard_reply(xcb_conn, i.value());

    if (attrs) {
        free(attrs);
        attrs = 0;
    }
    XFree(wmhints);
    damageTracking(false);
}

bool MWindowPropertyCache::hasAlpha()
{
    if (!is_valid || has_alpha != -1)
        return has_alpha == 1 ? true : false;
    has_alpha = 0;

    // the following code is replacing a XRenderFindVisualFormat() call...
    if (pict_formats_cookie.sequence) {
        Q_ASSERT(!pict_formats_reply);
        pict_formats_reply = xcb_render_query_pict_formats_reply(xcb_conn,
                                                   pict_formats_cookie, 0);
        pict_formats_cookie.sequence = 0;
        if (!pict_formats_reply) {
            qWarning("%s: querying pict formats has failed", __func__);
            return false;
        }
    }

    xcb_render_pictformat_t format = 0;
    xcb_render_pictscreen_iterator_t scr_i;
    scr_i = xcb_render_query_pict_formats_screens_iterator(pict_formats_reply);
    for (; scr_i.rem; xcb_render_pictscreen_next(&scr_i)) {
        xcb_render_pictdepth_iterator_t depth_i;
        depth_i = xcb_render_pictscreen_depths_iterator(scr_i.data);
        for (; depth_i.rem; xcb_render_pictdepth_next(&depth_i)) {
            xcb_render_pictvisual_iterator_t visual_i;
            visual_i = xcb_render_pictdepth_visuals_iterator(depth_i.data);
            for (; visual_i.rem; xcb_render_pictvisual_next(&visual_i)) {
                if (visual_i.data->visual == attrs->visual) {
                    format = visual_i.data->format;
                    break;
                }
            }
        }
    }
    // now we have the format, next find details for it
    xcb_render_pictforminfo_iterator_t pictform_i;
    pictform_i = xcb_render_query_pict_formats_formats_iterator(
                                                      pict_formats_reply);
    for (; pictform_i.rem; xcb_render_pictforminfo_next(&pictform_i)) {
        if (pictform_i.data->id == format) {
            has_alpha = pictform_i.data->direct.alpha_mask != 0 ? 1 : 0;
            break;
        }
    }

    return has_alpha;
}

const QRegion &MWindowPropertyCache::shapeRegion()
{
    QLatin1String me(SLOT(shapeRegion()));
    if (isUpdate(me))
        return shape_region;
    if (isInputOnly() || !requests.contains(me)) {
        // InputOnly window obstructs nothing
        cancelRequest(me);
        shape_region = QRegion(realGeometry());
        return shape_region;
    }

    xcb_shape_get_rectangles_cookie_t c = { requests[me] };
    xcb_shape_get_rectangles_reply_t *r;
    r = xcb_shape_get_rectangles_reply(xcb_conn, c, 0);
    replyCollected(me);
    if (!r) {
        shape_region = QRegion(realGeometry());
        return shape_region;
    }
    xcb_rectangle_iterator_t i;
    i = xcb_shape_get_rectangles_rectangles_iterator(r);
    shape_region = QRegion(0, 0, 0, 0);
    for (; i.rem; xcb_rectangle_next(&i))
        shape_region += QRect(i.data->x, i.data->y, i.data->width,
                              i.data->height);
    if (shape_region.isEmpty())
        shape_region = QRegion(realGeometry());
    free(r);
    return shape_region;
}

const QRegion &MWindowPropertyCache::customRegion()
{
    QLatin1String me(SLOT(customRegion()));
    if (is_valid && !requests.contains(me))
        requests[me] = requestProperty(MCompAtoms::_MEEGOTOUCH_CUSTOM_REGION,
                                       XCB_ATOM_CARDINAL, 10 * 4);
    else if (!is_valid || !requests[me])
        return custom_region;

    xcb_get_property_cookie_t c = { requests[me] };
    xcb_get_property_reply_t *r;
    r = xcb_get_property_reply(xcb_conn, c, 0);
    replyCollected(me);
    custom_region = QRegion(0, 0, 0, 0);
    if (r) {
        int len = xcb_get_property_value_length(r);
        if (len >= (int)sizeof(CARD32) * 4) {
            int n = len / sizeof(CARD32) / 4;
            CARD32 *p = (CARD32*)xcb_get_property_value(r);
            for (int i = 0; i < n; ++i) {
                 int j = i * 4;
                 QRect tmp(p[j], p[j + 1], p[j + 2], p[j + 3]);
                 custom_region += tmp;
            }
        }
        free(r);
    }
    return custom_region;
}

void MWindowPropertyCache::customRegion(bool request_only)
{
    Q_UNUSED(request_only);
    QLatin1String me(SLOT(customRegion()));
    Q_ASSERT(request_only);
    addRequest(me, requestProperty(MCompAtoms::_MEEGOTOUCH_CUSTOM_REGION,
                                   XCB_ATOM_CARDINAL, 10 * 4));
}

Window MWindowPropertyCache::transientFor()
{
    QLatin1String me(SLOT(transientFor()));
    if (is_valid && requests[me]) {
        xcb_get_property_cookie_t c = { requests[me] };
        xcb_get_property_reply_t *r;
        r = xcb_get_property_reply(xcb_conn, c, 0);
        replyCollected(me);
        transient_for = None;
        if (r) {
            if (xcb_get_property_value_length(r) == sizeof(Window))
                transient_for = *((Window*)xcb_get_property_value(r));
            free(r);
            if (transient_for == window)
                transient_for = 0;
            if (transient_for) {
                MCompositeManager *m = (MCompositeManager*)qApp;
                // add reference to the "parent"
                MWindowPropertyCache *p = m->d->prop_caches.value(
                                                        transient_for, 0);
                if (p) p->transients.append(window);
                // need to check stacking again to make sure the "parent" is
                // stacked according to the changed transient window list
                m->d->dirtyStacking(false);
            }
        }
    }
    return transient_for;
}

int MWindowPropertyCache::cannotMinimize()
{
    QLatin1String me(SLOT(cannotMinimize()));
    if (is_valid && requests[me]) {
        xcb_get_property_cookie_t c = { requests[me] };
        xcb_get_property_reply_t *r;
        r = xcb_get_property_reply(xcb_conn, c, 0);
        replyCollected(me);
        cannot_minimize = 0;
        if (r) {
            if (xcb_get_property_value_length(r) == sizeof(CARD32))
                cannot_minimize = *((CARD32*)xcb_get_property_value(r));
            free(r);
        }
    }
    return cannot_minimize;
}

int MWindowPropertyCache::alwaysMapped()
{
    QLatin1String me(SLOT(alwaysMapped()));
    if (is_valid && requests[me]) {
        xcb_get_property_cookie_t c = { requests[me] };
        xcb_get_property_reply_t *r;
        r = xcb_get_property_reply(xcb_conn, c, 0);
        replyCollected(me);
        always_mapped = 0;
        if (r) {
            if (xcb_get_property_value_length(r) == sizeof(CARD32))
                always_mapped = *((CARD32*)xcb_get_property_value(r));
            free(r);
        }
    }
    return always_mapped;
}

int MWindowPropertyCache::desktopView()
{
    QLatin1String me(SLOT(desktopView()));
    if (is_valid && !requests.contains(me))
        requests[me] = requestProperty(MCompAtoms::_MEEGOTOUCH_DESKTOP_VIEW,
                                       XCB_ATOM_CARDINAL);
    else if (!is_valid || !requests[me])
        return desktop_view;

    xcb_get_property_cookie_t c = { requests[me] };
    xcb_get_property_reply_t *r;
    r = xcb_get_property_reply(xcb_conn, c, 0);
    replyCollected(me);
    desktop_view = -1;
    if (r) {
        if (xcb_get_property_value_length(r) == sizeof(CARD32))
            desktop_view = *((CARD32*)xcb_get_property_value(r));
        free(r);
    }
    if (desktop_view < 0)
        // Try again next time we're called.
        requests.remove(me);

    return desktop_view;
}

void MWindowPropertyCache::desktopView(bool request_only)
{
    Q_UNUSED(request_only);
    QLatin1String me(SLOT(desktopView()));
    Q_ASSERT(request_only);
    addRequest(me, requestProperty(MCompAtoms::_MEEGOTOUCH_DESKTOP_VIEW,
                                   XCB_ATOM_CARDINAL));
}

bool MWindowPropertyCache::isDecorator()
{
    QLatin1String me(SLOT(isDecorator()));
    if (is_valid && requests[me]) {
        xcb_get_property_cookie_t c = { requests[me] };
        xcb_get_property_reply_t *r;
        r = xcb_get_property_reply(xcb_conn, c, 0);
        replyCollected(me);
        is_decorator = false;
        if (r) {
            if (xcb_get_property_value_length(r) == sizeof(CARD32))
                is_decorator = *((CARD32*)xcb_get_property_value(r));
            free(r);
        }
    }
    return is_decorator;
}

unsigned int MWindowPropertyCache::meegoStackingLayer()
{
    QLatin1String me(SLOT(meegoStackingLayer()));
    if (is_valid && requests[me]) {
        xcb_get_property_cookie_t c = { requests[me] };
        xcb_get_property_reply_t *r;
        r = xcb_get_property_reply(xcb_conn, c, 0);
        replyCollected(me);
        meego_layer = 0;
        if (r) {
            if (xcb_get_property_value_length(r) == sizeof(CARD32)) {
                meego_layer = *((CARD32*)xcb_get_property_value(r));
                if (meego_layer > 6)
                    meego_layer = 6;
            }
            free(r);
        }
    }
    return meego_layer;
}

/*!
 * Used for special windows that should not be minimised/iconified.
 */
bool MWindowPropertyCache::dontIconify()
{
    if (dont_iconify)
        return true;
    if (cannotMinimize() > 0)
        return true;
    int layer = meegoStackingLayer();
    if (layer == 1 || layer == 2)
        // these (screen/device lock) cannot be iconified by default
        return true;
    return false;
}

bool MWindowPropertyCache::wantsFocus()
{
    bool val = true;
    const XWMHints &h = getWMHints();
    if ((h.flags & InputHint) && (h.input == False))
        val = false;
    return val;
}

XID MWindowPropertyCache::windowGroup()
{
    XID val = 0;
    const XWMHints &h = getWMHints();
    if (h.flags & WindowGroupHint)
        val = h.window_group;
    return val;
}

const XWMHints &MWindowPropertyCache::getWMHints()
{
    QLatin1String me(SLOT(getWMHints()));
    if (is_valid && requests[me]) {
        xcb_get_property_cookie_t c = { requests[me] };
        xcb_get_property_reply_t *r;
        r = xcb_get_property_reply(xcb_conn, c, 0);
        replyCollected(me);
        if (r && xcb_get_property_value_length(r) >= int(sizeof(XWMHints)))
            memcpy(wmhints, xcb_get_property_value(r), sizeof(XWMHints));
        else
            memset(wmhints, 0, sizeof(*wmhints));
        free(r);
    }
    // @wmhints is always allocated
    return *wmhints;
}

bool MWindowPropertyCache::propertyEvent(XPropertyEvent *e)
{
    if (!is_valid)
        return false;
    if (e->atom == ATOM(WM_TRANSIENT_FOR)) {
        QLatin1String me(SLOT(transientFor()));
        if (isUpdate(me)) {
            MCompositeManager *m = (MCompositeManager*)qApp;
            // remove reference from the old "parent"
            MWindowPropertyCache *p = m->d->prop_caches.value(transient_for);
            if (p) p->transients.removeAll(window);
        }
        addRequest(me, requestProperty(e->atom, XCB_ATOM_WINDOW));
        return true;
    } else if (e->atom == ATOM(_MEEGOTOUCH_ALWAYS_MAPPED)) {
        addRequest(SLOT(alwaysMapped()),
                   requestProperty(e->atom, XCB_ATOM_CARDINAL));
        emit alwaysMappedChanged(this);
    } else if (e->atom == ATOM(_MEEGOTOUCH_CANNOT_MINIMIZE)) {
        addRequest(SLOT(cannotMinimize()),
                   requestProperty(e->atom, XCB_ATOM_CARDINAL));
    } else if (e->atom == ATOM(_MEEGOTOUCH_DESKTOP_VIEW)) {
        emit desktopViewChanged(this);
    } else if (e->atom == ATOM(WM_HINTS)) {
        addRequest(SLOT(getWMHints()),
                   requestProperty(e->atom, XCB_ATOM_WM_HINTS, 10));
        return true;
    } else if (e->atom == ATOM(_NET_WM_WINDOW_TYPE)) {
        addRequest(SLOT(windowTypeAtom()),
                   requestProperty(e->atom, XCB_ATOM_ATOM, MAX_TYPES));
        window_type = MCompAtoms::INVALID;
    } else if (e->atom == ATOM(_NET_WM_ICON_GEOMETRY)) {
        addRequest(SLOT(iconGeometry()),
                   requestProperty(e->atom, XCB_ATOM_CARDINAL, 4));
        emit iconGeometryUpdated();
    } else if (e->atom == ATOM(_MEEGOTOUCH_GLOBAL_ALPHA)) {
        addRequest(SLOT(globalAlpha()),
                   requestProperty(e->atom, XCB_ATOM_CARDINAL));
    } else if (e->atom == ATOM(_MEEGOTOUCH_VIDEO_ALPHA)) {
        addRequest(SLOT(videoGlobalAlpha()),
                   requestProperty(e->atom, XCB_ATOM_CARDINAL));
    } else if (e->atom == ATOM(_MEEGOTOUCH_DECORATOR_BUTTONS)) {
        addRequest(SLOT(buttonGeometryHelper()),
                   requestProperty(e->atom, XCB_ATOM_CARDINAL, 8));
        emit meegoDecoratorButtonsChanged(window);
    } else if (e->atom == ATOM(_MEEGOTOUCH_ORIENTATION_ANGLE)) {
        addRequest(SLOT(orientationAngle()),
              requestProperty(MCompAtoms::_MEEGOTOUCH_ORIENTATION_ANGLE,
                              XCB_ATOM_CARDINAL));
    } else if (e->atom == ATOM(_MEEGOTOUCH_MSTATUSBAR_GEOMETRY)) {
        addRequest(SLOT(statusbarGeometry()),
            requestProperty(e->atom, XCB_ATOM_CARDINAL, 4));
    } else if (e->atom == ATOM(WM_PROTOCOLS)) {
        addRequest(SLOT(supportedProtocols()),
                   requestProperty(e->atom, XCB_ATOM_ATOM, 100));
    } else if (e->atom == ATOM(_NET_WM_STATE)) {
        addRequest(SLOT(netWmState()),
                   requestProperty(e->atom, XCB_ATOM_ATOM, 100));
        return false;
    } else if (e->atom == ATOM(WM_STATE)) {
        addRequest(SLOT(windowState()),
                        requestProperty(e->atom, ATOM(WM_STATE)));
        return true;
    } else if (e->atom == ATOM(_MEEGO_STACKING_LAYER)) {
        addRequest(SLOT(meegoStackingLayer()),
                   requestProperty(e->atom, XCB_ATOM_CARDINAL));
        if (window_state == NormalState) {
            // raise it so that it becomes on top of same-leveled windows
            MCompositeManager *m = (MCompositeManager*)qApp;
            m->d->positionWindow(window, true);
        }
        return true;
    } else if (e->atom == ATOM(_MEEGOTOUCH_CUSTOM_REGION)) {
        emit customRegionChanged(this);
    } else if (e->atom == ATOM(WM_NAME)) {
        addRequest(SLOT(wmName()),
                   requestProperty(MCompAtoms::WM_NAME, XCB_ATOM_STRING, 100));
    }
    return false;
}

int MWindowPropertyCache::windowState()
{
    QLatin1String me(SLOT(windowState()));
    if (requests[me]) {
        xcb_get_property_cookie_t c = { requests[me] };
        xcb_get_property_reply_t *r;
        r = xcb_get_property_reply(xcb_conn, c, 0);
        replyCollected(me);
        if (r && xcb_get_property_value_length(r) >= int(sizeof(CARD32)))
            window_state = ((CARD32*)xcb_get_property_value(r))[0];
        else {
            // mark it so that MCompositeManagerPrivate::setWindowState sets it
            window_state = -1;
        }
        free(r);
    }
    return window_state;
}

void MWindowPropertyCache::setWindowState(int state)
{
    // The window's type is about to change.  Change the the idea of the
    // property cache about the window's type now to make windowState()
    // non-blocking.
    cancelRequest(SLOT(windowState()));
    window_state = state;
}

void MWindowPropertyCache::buttonGeometryHelper()
{
    QLatin1String me(SLOT(buttonGeometryHelper()));
    if (!is_valid || !requests[me])
        return;

    xcb_get_property_cookie_t c = { requests[me] };
    xcb_get_property_reply_t *r;
    r = xcb_get_property_reply(xcb_conn, c, 0);
    replyCollected(me);
    if (!r)
        return;
    int len = xcb_get_property_value_length(r);
    if (len == 8 * sizeof(CARD32)) {
        CARD32* coords = (CARD32*)xcb_get_property_value(r);
        home_button_geom.setRect(coords[0], coords[1], coords[2], coords[3]);
        close_button_geom.setRect(coords[4], coords[5], coords[6], coords[7]);
    } else if (len != 0)
        qWarning("%s: _MEEGOTOUCH_DECORATOR_BUTTONS size is %d", __func__, len);
    free(r);
}

const QRect &MWindowPropertyCache::homeButtonGeometry()
{
    buttonGeometryHelper();
    return home_button_geom;
}

const QRect &MWindowPropertyCache::closeButtonGeometry()
{
    buttonGeometryHelper();
    return close_button_geom;
}

unsigned MWindowPropertyCache::orientationAngle()
{
    QLatin1String me(SLOT(orientationAngle()));
    if (!is_valid || !requests[me])
        return orientation_angle;

    xcb_get_property_cookie_t c = { requests[me] };
    xcb_get_property_reply_t *r;
    r = xcb_get_property_reply(xcb_conn, c, 0);
    replyCollected(me);
    orientation_angle = 0;
    if (r != NULL) {
        if (xcb_get_property_value_length(r) == sizeof(CARD32))
            orientation_angle = *((CARD32*)xcb_get_property_value(r));
        free(r);
    }

    return orientation_angle;
}

const QRect &MWindowPropertyCache::statusbarGeometry()
{
    QLatin1String me(SLOT(statusbarGeometry()));
    if (!is_valid || !requests[me])
        return statusbar_geom;

    xcb_get_property_cookie_t c = { requests[me] };
    xcb_get_property_reply_t *r;
    r = xcb_get_property_reply(xcb_conn, c, 0);
    replyCollected(me);
    statusbar_geom.setRect(0, 0, 0, 0);
    if (r && xcb_get_property_value_length(r) == int(4*sizeof(CARD32))) {
        CARD32* coords = (CARD32 *)xcb_get_property_value(r);
        statusbar_geom.setRect(coords[0], coords[1], coords[2], coords[3]);
    } else
        statusbar_geom.setRect(0, 0, 0, 0);
    free(r);
    return statusbar_geom;
}

const QList<Atom>& MWindowPropertyCache::supportedProtocols()
{
    QLatin1String me(SLOT(supportedProtocols()));
    if (!is_valid || !requests[me])
        return wm_protocols;

    xcb_get_property_cookie_t c = { requests[me] };
    xcb_get_property_reply_t *r;
    r = xcb_get_property_reply(xcb_conn, c, 0);
    replyCollected(me);
    wm_protocols.clear();
    if (!r)
        return wm_protocols;
    int n_atoms = xcb_get_property_value_length(r) / sizeof(Atom);
    Atom* atoms = (Atom*)xcb_get_property_value(r);
    for (int i = 0; i < n_atoms; ++i)
        wm_protocols.append(atoms[i]);
    free(r);
    return wm_protocols;
}

const QList<Atom> &MWindowPropertyCache::netWmState()
{
    QLatin1String me(SLOT(netWmState()));
    if (!is_valid || !requests[me])
        return net_wm_state;

    xcb_get_property_cookie_t c = { requests[me] };
    xcb_get_property_reply_t *r;
    r = xcb_get_property_reply(xcb_conn, c, 0);
    replyCollected(me);
    net_wm_state.clear();
    if (!r)
        return net_wm_state;
    int n_atoms = xcb_get_property_value_length(r) / sizeof(Atom);
    Atom* atoms = (Atom*)xcb_get_property_value(r);
    for (int i = 0; i < n_atoms; ++i)
        net_wm_state.append(atoms[i]);
    free(r);
    return net_wm_state;
}

void MWindowPropertyCache::setNetWmState(const QList<Atom>& s) {
    if (!is_valid)
        return;
    cancelRequest(SLOT(netWmState()));
    net_wm_state = s;
}

const QRectF &MWindowPropertyCache::iconGeometry()
{
    QLatin1String me(SLOT(iconGeometry()));
    if (!is_valid || !requests[me])
        return icon_geometry;

    xcb_get_property_cookie_t c = { requests[me] };
    xcb_get_property_reply_t *r;
    r = xcb_get_property_reply(xcb_conn, c, 0);
    replyCollected(me);
    if (r && xcb_get_property_value_length(r) >= int(4*sizeof(CARD32))) {
        CARD32* coords = (CARD32*)xcb_get_property_value(r);
        icon_geometry.setRect(coords[0], coords[1], coords[2], coords[3]);
    } else
        icon_geometry = QRectF();
    free(r);
    return icon_geometry;
}

int MWindowPropertyCache::alphaValue(const QLatin1String me)
{
    xcb_get_property_cookie_t c = { requests[me] };
    xcb_get_property_reply_t *r;
    r = xcb_get_property_reply(xcb_conn, c, 0);
    replyCollected(me);
    if (!r) 
        return 255;
    
    if (xcb_get_property_value_length(r) < int(sizeof(CARD32))) {
        free(r);
        return 255;
    }

    /* Map 0..0xFFFFFFFF -> 0..0xFF. */
    int ret = *(CARD32*)xcb_get_property_value(r) >> 24;
    free(r);
    return ret;
}

int MWindowPropertyCache::globalAlpha()
{
    QLatin1String me(SLOT(globalAlpha()));
    if (is_valid && requests[me])
        global_alpha = alphaValue(me);
    return global_alpha;
}

int MWindowPropertyCache::videoGlobalAlpha()
{    
    QLatin1String me(SLOT(videoGlobalAlpha()));
    if (is_valid && requests[me])
        video_global_alpha = alphaValue(me);
    return video_global_alpha;
}

Atom MWindowPropertyCache::windowTypeAtom()
{
    QLatin1String me(SLOT(windowTypeAtom()));
    if (!is_valid)
        return None;
    if (!requests[me]) {
        Q_ASSERT(!type_atoms.isEmpty());
        return type_atoms[0];
    }

    type_atoms.resize(0);
    xcb_get_property_cookie_t c = { requests[me] };
    xcb_get_property_reply_t *r;
    r = xcb_get_property_reply(xcb_conn, c, 0);
    replyCollected(me);
    if (r) {
        int n = xcb_get_property_value_length(r) / (int)sizeof(Atom);
        if (n > 0) {
            type_atoms.resize(n);
            memcpy(type_atoms.data(), xcb_get_property_value(r),
                   xcb_get_property_value_length(r));
        }
        free(r);
    }

    if (type_atoms.isEmpty()) {
        type_atoms.resize(1);
        type_atoms[0] = ATOM(_NET_WM_WINDOW_TYPE_NORMAL);
    }

    return type_atoms[0];
}

MCompAtoms::Type MWindowPropertyCache::windowType()
{
    if (!is_valid)
        return MCompAtoms::INVALID;
    else if (window_type != MCompAtoms::INVALID)
        return window_type;

    Atom type_atom = windowTypeAtom();
    if (type_atom == ATOM(_NET_WM_WINDOW_TYPE_DESKTOP))
        window_type = MCompAtoms::DESKTOP;
    else if (type_atom == ATOM(_NET_WM_WINDOW_TYPE_NORMAL))
        window_type = MCompAtoms::NORMAL;
    else if (type_atom == ATOM(_NET_WM_WINDOW_TYPE_DIALOG)) {
        if (type_atoms.contains(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE)))
            window_type = MCompAtoms::NO_DECOR_DIALOG;
        else
            window_type = MCompAtoms::DIALOG;
    } else if (type_atom == ATOM(_NET_WM_WINDOW_TYPE_DOCK))
        window_type = MCompAtoms::DOCK;
    else if (type_atom == ATOM(_NET_WM_WINDOW_TYPE_INPUT))
        window_type = MCompAtoms::INPUT;
    else if (type_atom == ATOM(_NET_WM_WINDOW_TYPE_NOTIFICATION))
        window_type = MCompAtoms::NOTIFICATION;
    else if (type_atom == ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE) ||
             type_atom == ATOM(_NET_WM_WINDOW_TYPE_MENU))
        window_type = MCompAtoms::FRAMELESS;
    else if (transientFor())
        window_type = MCompAtoms::UNKNOWN;
    else // fdo spec suggests unknown non-transients must be normal
        window_type = MCompAtoms::NORMAL;
    return window_type;
}

void MWindowPropertyCache::setRealGeometry(const QRect &rect)
{
    QLatin1String me(SLOT(realGeometry()));
    if (!is_valid)
        return;

    cancelRequest(me);
    real_geom = rect;

    // shape needs to be refreshed in case it was the default value
    // (i.e. the same as geometry), because there is no ShapeNotify
    if (!isUpdate(SLOT(shapeRegion())) || QRegion(real_geom) != shape_region)
        shapeRefresh();
}

const QRect MWindowPropertyCache::realGeometry()
{
    QLatin1String me(SLOT(realGeometry()));
    if (is_valid && requests[me]) {
        xcb_get_geometry_cookie_t c = { requests[me] };
        xcb_get_geometry_reply_t *xcb_real_geom;
        xcb_real_geom = xcb_get_geometry_reply(xcb_conn, c, 0);
        replyCollected(me);
        if (xcb_real_geom) {
            // We can set @real_geom because setRealGeom() would have
            // cancelRequest()ed us if it was set explicitly.
            real_geom = QRect(xcb_real_geom->x, xcb_real_geom->y,
                              xcb_real_geom->width, xcb_real_geom->height);
            free(xcb_real_geom);
        }
    }
    return real_geom;
}

const QString &MWindowPropertyCache::wmName()
{
    QLatin1String me(SLOT(wmName()));
    if (is_valid && requests[me]) {
        xcb_get_property_cookie_t c = { requests[me] };
        xcb_get_property_reply_t *r;
        r = xcb_get_property_reply(xcb_conn, c, 0);
        replyCollected(me);
        if (r) {
            int len = xcb_get_property_value_length(r);
            if (len > 0) {
                char buf[len + 1];
                memcpy(buf, xcb_get_property_value(r), len);
                buf[len] = '\0';
                wm_name = buf;
            } else
                wm_name = "";
            free(r);
        }
    }
    return wm_name;
}

void MWindowPropertyCache::shapeRefresh()
{
    if (!is_valid)
        return;
    addRequest(SLOT(shapeRegion()),
               xcb_shape_get_rectangles(xcb_conn, window,
                                        ShapeBounding).sequence);
}

// MWindowDummyPropertyCache
MWindowDummyPropertyCache *MWindowDummyPropertyCache::singleton;

MWindowDummyPropertyCache *MWindowDummyPropertyCache::get()
{
    if (!singleton)
        singleton = new MWindowDummyPropertyCache();
    return singleton;
}

bool MWindowDummyPropertyCache::event(QEvent *e)
{
    // Ignore deleteLater().
    return e->type() == QEvent::DeferredDelete
        ? true : MWindowPropertyCache::event(e);
}
