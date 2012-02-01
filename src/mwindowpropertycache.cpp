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
bool MWindowPropertyCache::isUpdate(const CollectorKey key)
{
    return requests[key].requested && !requests[key].cookie;
}

// Called when @collector's property is being queried, and it sets up
// a timer to collect the reply in a while.  If a query is already ongoing
// it's cancelled.  @cookie should be what xcb_*() returned.
void MWindowPropertyCache::addRequest(const CollectorKey key,
                                      const QLatin1String &collector,
                                      unsigned cookie)
{
    if (is_virtual)
        return;

    unsigned prev_cookie = requests[key].cookie;
    requests[key].cookie = cookie;
    requests[key].name = collector;
    requests[key].requested = 1;
    if (prev_cookie)
        xcb_discard_reply(xcb_conn, prev_cookie);
    else
        connect(collect_timer, SIGNAL(timeout()), this, collector.latin1());
    collect_timer->start();
}

// Makes @collector's property considered isUpdate().
void MWindowPropertyCache::replyCollected(const CollectorKey key)
{
    Q_ASSERT(!is_virtual);
    QLatin1String collector = requests[key].name;
    requests[key].name = QLatin1String("");
    requests[key].cookie = 0;
    collect_timer->disconnect(collector.latin1());
}

// If @collector has an ongoing query, cancels it.  @collector's property
// will have been considered isUpdate().
void MWindowPropertyCache::cancelRequest(const CollectorKey key)
{
    unsigned cookie = requests[key].cookie;
    if (cookie) {
        xcb_discard_reply(xcb_conn, cookie);
        replyCollected(key);
    } else {
        requests[key].name = QLatin1String("");
        requests[key].cookie = 0;
    }
}

// some unit tests want to fake window properties
void MWindowPropertyCache::cancelAllRequests()
{
    for (int i = 0; i < lastCollectorKey; ++i)
        cancelRequest((CollectorKey)i);
}

// Shorthand to request the value of a property.  Returns what you can
// pass to addRequest().
unsigned MWindowPropertyCache::requestProperty(Atom prop, Atom type,
                                               unsigned n)
{
    Q_ASSERT(!is_virtual);
    return xcb_get_property(xcb_conn, 0, window,
                            prop, type, 0, n).sequence;
}

xcb_connection_t *MWindowPropertyCache::xcb_conn;

void MWindowPropertyCache::init()
{
    wm_pid = 0;
    transient_for = None,
    invoked_by = None,
    has_alpha = -1;
    global_alpha = 255;
    video_global_alpha = 255;
    is_decorator = false;
    force_skipping_taskbar = false;
    wmhints = XAllocWMHints();
    attrs = 0;
    meego_layer = 0;
    low_power_mode = 0;
    opaque_window = 0;
    prestarted = false;
    window_state = -1;
    window_type = MCompAtoms::INVALID;
    parent_window = QX11Info::appRootWindow();
    always_mapped = 0;
    cannot_minimize = 0;
    desktop_view = -1;
    being_mapped = false;
    dont_iconify = false;
    stacked_unmapped = false;
    orientation_angle = 0;
    damage_object = 0;
    damage_report_level = -1;
    collect_timer = 0;
    no_animations = 0;
    video_overlay = 0;
    pending_damage = false;
    skipping_taskbar_marker = false;
    waiting_for_damage = 0;
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
    is_virtual = window == None;
    if (!wa) {
        attrs = xcb_get_window_attributes_reply(xcb_conn,
                        xcb_get_window_attributes(xcb_conn, window), 0);
        if (!attrs) {
            //qWarning("%s: invalid window 0x%lx", __func__, window);
            init_invalid();
            free(geom);
            if (damage_obj)
                XDamageDestroy(QX11Info::display(), damage_obj);
            return;
        }
    } else
        attrs = wa;

    is_valid = true;
    damage_object = damage_obj;

    if (geom) {
        real_geom = QRect(geom->x, geom->y, geom->width, geom->height);
        requests[realGeometryKey].requested = 1;
        free(geom);
    }

    if (is_virtual)
        // addRequest() is NOP
        return;

    if (!isMapped()) {
        // required to get property changes happening before mapping
        // (after mapping, MCompositeManager sets the window's input mask)
        XSelectInput(QX11Info::display(), window, PropertyChangeMask);
        XShapeSelectInput(QX11Info::display(), window, ShapeNotifyMask);
    }

    collect_timer = new MCSmartTimer(this);
    collect_timer->setInterval(5000);
    collect_timer->setSingleShot(true);

    if (!geom)
        addRequest(realGeometryKey, SLOT(realGeometry()),
                   xcb_get_geometry(xcb_conn, window).sequence);
    addRequest(isDecoratorKey, SLOT(isDecorator()),
               requestProperty(MCompAtoms::_MEEGOTOUCH_DECORATOR_WINDOW,
                               XCB_ATOM_CARDINAL));
    addRequest(transientForKey, SLOT(transientFor()),
               requestProperty(XCB_ATOM_WM_TRANSIENT_FOR,
                               XCB_ATOM_WINDOW));
    addRequest(invokedByKey, SLOT(invokedBy()),
               requestProperty(MCompAtoms::_MEEGOTOUCH_WM_INVOKED_BY,
                               XCB_ATOM_WINDOW));
    addRequest(meegoStackingLayerKey, SLOT(meegoStackingLayer()),
               requestProperty(MCompAtoms::_MEEGO_STACKING_LAYER,
                               XCB_ATOM_CARDINAL));
    addRequest(lowPowerModeKey, SLOT(lowPowerMode()),
               requestProperty(MCompAtoms::_MEEGO_LOW_POWER_MODE,
                               XCB_ATOM_CARDINAL));
    addRequest(opaqueWindowKey, SLOT(opaqueWindow()),
               requestProperty(MCompAtoms::_MEEGOTOUCH_OPAQUE_WINDOW,
                               XCB_ATOM_CARDINAL));
    addRequest(prestartedAppKey, SLOT(prestartedApp()),
               requestProperty(MCompAtoms::_MEEGOTOUCH_PRESTARTED,
                               XCB_ATOM_CARDINAL));
    addRequest(windowTypeAtomKey, SLOT(windowTypeAtom()),
               requestProperty(MCompAtoms::_NET_WM_WINDOW_TYPE,
                               XCB_ATOM_ATOM, MAX_TYPES));
    if (!pict_formats_reply && !pict_formats_cookie.sequence)
        pict_formats_cookie = xcb_render_query_pict_formats(xcb_conn);
    addRequest(orientationAngleKey, SLOT(orientationAngle()),
               requestProperty(MCompAtoms::_MEEGOTOUCH_ORIENTATION_ANGLE,
                               XCB_ATOM_CARDINAL));
    addRequest(statusbarGeometryKey, SLOT(statusbarGeometry()),
               requestProperty(MCompAtoms::_MEEGOTOUCH_MSTATUSBAR_GEOMETRY,
                               XCB_ATOM_CARDINAL, 4));
    addRequest(supportedProtocolsKey, SLOT(supportedProtocols()),
               requestProperty(MCompAtoms::WM_PROTOCOLS,
                               XCB_ATOM_ATOM, 100));
    addRequest(windowStateKey, SLOT(windowState()),
               requestProperty(MCompAtoms::WM_STATE, ATOM(WM_STATE)));
    addRequest(getWMHintsKey, SLOT(getWMHints()),
               requestProperty(XCB_ATOM_WM_HINTS, XCB_ATOM_WM_HINTS, 10));
    addRequest(iconGeometryKey, SLOT(iconGeometry()),
               requestProperty(MCompAtoms::_NET_WM_ICON_GEOMETRY,
                               XCB_ATOM_CARDINAL, 4));
    addRequest(globalAlphaKey, SLOT(globalAlpha()),
               requestProperty(MCompAtoms::_MEEGOTOUCH_GLOBAL_ALPHA,
                                XCB_ATOM_CARDINAL));
    addRequest(videoGlobalAlphaKey, SLOT(videoGlobalAlpha()),
               requestProperty(MCompAtoms::_MEEGOTOUCH_VIDEO_ALPHA,
                                XCB_ATOM_CARDINAL));
    if (!isInputOnly())
        addRequest(shapeRegionKey, SLOT(shapeRegion()),
                   xcb_shape_get_rectangles(xcb_conn, window,
                                            ShapeBounding).sequence);
    addRequest(netWmStateKey, SLOT(netWmState()),
               requestProperty(MCompAtoms::_NET_WM_STATE,
                               XCB_ATOM_ATOM, 100));
    addRequest(alwaysMappedKey, SLOT(alwaysMapped()),
               requestProperty(MCompAtoms::_MEEGOTOUCH_ALWAYS_MAPPED,
                                XCB_ATOM_CARDINAL));
    addRequest(cannotMinimizeKey, SLOT(cannotMinimize()),
               requestProperty(MCompAtoms::_MEEGOTOUCH_CANNOT_MINIMIZE,
                                XCB_ATOM_CARDINAL));
    addRequest(wmNameKey, SLOT(wmName()),
               requestProperty(MCompAtoms::WM_NAME, XCB_ATOM_STRING, 100));
    addRequest(pidKey, SLOT(pid()), requestProperty(MCompAtoms::_NET_WM_PID,
                                                    XCB_ATOM_CARDINAL));
    addRequest(noAnimationsKey, SLOT(noAnimations()),
               requestProperty(MCompAtoms::_MEEGOTOUCH_NO_ANIMATIONS,
                               XCB_ATOM_CARDINAL));
    addRequest(videoOverlayKey, SLOT(videoOverlay()),
               requestProperty(MCompAtoms::_OMAP_VIDEO_OVERLAY,
                               XCB_ATOM_INTEGER));
    addRequest(skippingTaskbarMarkerKey, SLOT(skippingTaskbarMarker()),
               requestProperty(MCompAtoms::_MCOMPOSITOR_SKIP_TASKBAR,
                               XCB_ATOM_CARDINAL));

    // add any transients to the transients list
    MCompositeManager *m = (MCompositeManager*)qApp;
    for (QList<Window>::const_iterator it = m->d->stacking_list.begin();
         it != m->d->stacking_list.end(); ++it) {
        MWindowPropertyCache *p = m->d->prop_caches.value(*it, 0);
        if (p && p != this && p->transientFor() == window)
            transients.append(*it);
    }
}

MWindowPropertyCache::MWindowPropertyCache()
    : window(None)
{
    init();
    init_invalid();
}

MWindowPropertyCache::~MWindowPropertyCache()
{
    if (!is_valid || is_virtual) {
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
    for (int i = 0; i < lastCollectorKey; ++i)
      if (requests[i].cookie)
          xcb_discard_reply(xcb_conn, requests[i].cookie);

    free(attrs);
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
    const CollectorKey me = shapeRegionKey;
    if (requests[me].requested && !requests[me].cookie)
        return shape_region;
    if (isInputOnly() || !requests[me].requested) {
        // InputOnly window obstructs nothing
        cancelRequest(me);
        QRect r = realGeometry();
        // shape is relative to window's position, unlike geometry
        r.translate(-r.x(), -r.y());
        shape_region = QRegion(r);
        return shape_region;
    }

    xcb_shape_get_rectangles_cookie_t c = { requests[me].cookie };
    xcb_shape_get_rectangles_reply_t *r;
    r = xcb_shape_get_rectangles_reply(xcb_conn, c, 0);
    replyCollected(me);
    if (!r) {
        QRect r = realGeometry();
        r.translate(-r.x(), -r.y());
        shape_region = QRegion(r);
        return shape_region;
    }
    xcb_rectangle_iterator_t i;
    i = xcb_shape_get_rectangles_rectangles_iterator(r);
    shape_region = QRegion(0, 0, 0, 0);
    for (; i.rem; xcb_rectangle_next(&i))
        shape_region += QRect(i.data->x, i.data->y, i.data->width,
                              i.data->height);
    if (shape_region.isEmpty()) {
        QRect r = realGeometry();
        r.translate(-r.x(), -r.y());
        shape_region = QRegion(r);
    }
    free(r);
    return shape_region;
}

const QRegion &MWindowPropertyCache::customRegion()
{
    const CollectorKey me = customRegionKey;
    if (is_valid && !is_virtual && !requests[me].requested) {
        requests[me].cookie = requestProperty(MCompAtoms::_MEEGOTOUCH_CUSTOM_REGION,
                                              XCB_ATOM_CARDINAL, 10 * 4);
        requests[me].name = QLatin1String(SLOT(customRegion()));
        requests[me].requested = 1;
    } else if (!is_valid || !requests[me].requested || !requests[me].cookie)
        return custom_region;

    xcb_get_property_reply_t *r;
    xcb_get_property_cookie_t c = { requests[me].cookie };
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
    addRequest(customRegionKey,
               me, requestProperty(MCompAtoms::_MEEGOTOUCH_CUSTOM_REGION,
                                   XCB_ATOM_CARDINAL, 10 * 4));
}

Window MWindowPropertyCache::transientFor()
{
    const CollectorKey me = transientForKey;
    if (is_valid && requests[me].requested && requests[me].cookie) {
        xcb_get_property_reply_t *r;
        xcb_get_property_cookie_t c = { requests[me].cookie };
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

Window MWindowPropertyCache::invokedBy()
{
    const CollectorKey me = invokedByKey;
    if (is_valid && requests[me].requested && requests[me].cookie) {
        xcb_get_property_reply_t *r;
        xcb_get_property_cookie_t c = { requests[me].cookie };
        r = xcb_get_property_reply(xcb_conn, c, 0);
        replyCollected(me);
        invoked_by = None;
        if (r) {
            if (xcb_get_property_value_length(r) == sizeof(Window))
                invoked_by = *((Window*)xcb_get_property_value(r));
            free(r);
        }
    }
    return invoked_by;    
}

int MWindowPropertyCache::cannotMinimize()
{
    CARD32 val;
    if (is_valid && getCARD32(cannotMinimizeKey, &val))
        cannot_minimize = val;
    return cannot_minimize;
}

unsigned int MWindowPropertyCache::noAnimations()
{
    CARD32 val;
    if (is_valid && getCARD32(noAnimationsKey, &val))
        no_animations = val;
    return no_animations;
}

int MWindowPropertyCache::videoOverlay()
{
    const CollectorKey me = videoOverlayKey;
    if (is_valid && requests[me].requested && requests[me].cookie) {
        xcb_get_property_reply_t *r;
        xcb_get_property_cookie_t c = { requests[me].cookie };
        r = xcb_get_property_reply(xcb_conn, c, 0);
        replyCollected(me);
        video_overlay = 0;
        if (r) {
            if (xcb_get_property_value_length(r) == sizeof(INT8))
                video_overlay = *((INT8*)xcb_get_property_value(r));
            free(r);
        }
    }
    return video_overlay;
}



int MWindowPropertyCache::alwaysMapped()
{
    const CollectorKey me = alwaysMappedKey;
    if (is_valid && requests[me].requested && requests[me].cookie) {
        xcb_get_property_reply_t *r;
        xcb_get_property_cookie_t c = { requests[me].cookie };
        r = xcb_get_property_reply(xcb_conn, c, 0);
        replyCollected(me);
        always_mapped = 0;
        if (r) {
            if (xcb_get_property_value_length(r) == sizeof(CARD32))
                always_mapped = *((CARD32*)xcb_get_property_value(r));
            free(r);
        }
        if (!always_mapped && windowType() == MCompAtoms::DOCK)
            always_mapped = 1000;
    }
    return always_mapped;
}

int MWindowPropertyCache::desktopView()
{
    const CollectorKey me = desktopViewKey;
    if (is_valid && !is_virtual && !requests[me].requested) {
        requests[me].cookie = requestProperty(MCompAtoms::_MEEGOTOUCH_DESKTOP_VIEW,
                                              XCB_ATOM_CARDINAL);
        requests[me].name = QLatin1String(SLOT(desktopView()));
        requests[me].requested = 1;
    } else if (!is_valid || !requests[me].requested || !requests[me].cookie)
        return desktop_view;

    xcb_get_property_reply_t *r;
    xcb_get_property_cookie_t c = { requests[me].cookie };
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
        requests[me].requested = 0;

    return desktop_view;
}

void MWindowPropertyCache::desktopView(bool request_only)
{
    Q_UNUSED(request_only);
    QLatin1String me(SLOT(desktopView()));
    Q_ASSERT(request_only);
    addRequest(desktopViewKey,
               me, requestProperty(MCompAtoms::_MEEGOTOUCH_DESKTOP_VIEW,
                                   XCB_ATOM_CARDINAL));
}

// returns true if there was a reply with valid value or if the property
// is not there
bool MWindowPropertyCache::getCARD32(const CollectorKey me, CARD32 *value)
{
    if (!is_valid || !requests[me].requested || !requests[me].cookie)
        return false;
    xcb_get_property_reply_t *r;
    xcb_get_property_cookie_t c = { requests[me].cookie };
    r = xcb_get_property_reply(xcb_conn, c, 0);
    replyCollected(me);
    if (r) {
        if (xcb_get_property_value_length(r) == sizeof(CARD32)) {
            *value = *((CARD32*)xcb_get_property_value(r));
            free(r);
            return true;
        }
        free(r);
    }
    // property is missing, we need to return 0, in case it was set before
    // and then deleted
    *value = 0;
    return true;
}

bool MWindowPropertyCache::isDecorator()
{
    CARD32 val;
    if (is_valid && getCARD32(isDecoratorKey, &val))
        is_decorator = !!val;
    return is_decorator;
}

unsigned int MWindowPropertyCache::meegoStackingLayer()
{
    CARD32 val;
    if (is_valid && getCARD32(meegoStackingLayerKey, &val))
        meego_layer = val > 10 ? 10 : val;
    return meego_layer;
}

unsigned int MWindowPropertyCache::lowPowerMode()
{
    CARD32 val;
    if (is_valid && getCARD32(lowPowerModeKey, &val))
        low_power_mode = val;
    return low_power_mode;
}

unsigned int MWindowPropertyCache::opaqueWindow()
{
    CARD32 val;
    if (is_valid && getCARD32(opaqueWindowKey, &val))
        opaque_window = val;
    return opaque_window;
}

bool MWindowPropertyCache::prestartedApp()
{
    const CollectorKey me = prestartedAppKey;
    if (is_valid && requests[me].requested && requests[me].cookie) {
        xcb_get_property_reply_t *r;
        xcb_get_property_cookie_t c = { requests[me].cookie };
        r = xcb_get_property_reply(xcb_conn, c, 0);
        replyCollected(me);
        // some bright soul decided to make it 8-bit..
        if (r && xcb_get_property_value_length(r) == sizeof(char)
            && *((char*)xcb_get_property_value(r)))
            prestarted = true;
        free(r);
    }
    return prestarted;
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
    if (layer == 1 || isLockScreen())
        // these (screen/device lock) cannot be iconified by default
        return true;
    return false;
}

bool MWindowPropertyCache::isLockScreen()
{
    return (meegoStackingLayer() > 0 && wmName() == "Screen Lock");
}

bool MWindowPropertyCache::isCallUi()
{
    return (wmName() == "call-ui");
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
    const CollectorKey me = getWMHintsKey;
    if (is_valid && requests[me].requested && requests[me].cookie) {
        xcb_get_property_reply_t *r;
        xcb_get_property_cookie_t c = { requests[me].cookie };
        r = xcb_get_property_reply(xcb_conn, c, 0);
        replyCollected(me);
        if (r && xcb_get_property_value_length(r) >= int(sizeof(XWMHints))) {
            memcpy(wmhints, xcb_get_property_value(r), sizeof(XWMHints));
            if (prestartedApp())
                // Ignore the bogus iconic initial_state requests
                // of prestarted apps.
                wmhints->flags &= ~StateHint;
        } else
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
        const CollectorKey me = transientForKey;
        if (isUpdate(me)) {
            MCompositeManager *m = (MCompositeManager*)qApp;
            // remove reference from the old "parent"
            MWindowPropertyCache *p = m->d->prop_caches.value(transient_for);
            if (p) p->transients.removeAll(window);
        }
        addRequest(me, SLOT(transientFor()),
                   requestProperty(e->atom, XCB_ATOM_WINDOW));
        return true;
    } else if (e->atom == ATOM(_MEEGOTOUCH_WM_INVOKED_BY)) {
        addRequest(invokedByKey, SLOT(invokedBy()),
                   requestProperty(e->atom, XCB_ATOM_WINDOW));
        return true;
    } else if (e->atom == ATOM(_MEEGOTOUCH_ALWAYS_MAPPED)) {
        addRequest(alwaysMappedKey, SLOT(alwaysMapped()),
                   requestProperty(e->atom, XCB_ATOM_CARDINAL));
        emit alwaysMappedChanged(this);
    } else if (e->atom == ATOM(_MEEGOTOUCH_CANNOT_MINIMIZE)) {
        addRequest(cannotMinimizeKey, SLOT(cannotMinimize()),
                   requestProperty(e->atom, XCB_ATOM_CARDINAL));
    } else if (e->atom == ATOM(_MEEGOTOUCH_DESKTOP_VIEW)) {
        emit desktopViewChanged(this);
    } else if (e->atom == ATOM(WM_HINTS)) {
        addRequest(getWMHintsKey, SLOT(getWMHints()),
                   requestProperty(e->atom, XCB_ATOM_WM_HINTS, 10));
        return true;
    } else if (e->atom == ATOM(_NET_WM_WINDOW_TYPE)) {
        addRequest(windowTypeAtomKey, SLOT(windowTypeAtom()),
                   requestProperty(e->atom, XCB_ATOM_ATOM, MAX_TYPES));
        window_type = MCompAtoms::INVALID;
        return true;
    } else if (e->atom == ATOM(_NET_WM_ICON_GEOMETRY)) {
        addRequest(iconGeometryKey, SLOT(iconGeometry()),
                   requestProperty(e->atom, XCB_ATOM_CARDINAL, 4));
        emit iconGeometryUpdated();
    } else if (e->atom == ATOM(_MEEGOTOUCH_GLOBAL_ALPHA)) {
        addRequest(globalAlphaKey, SLOT(globalAlpha()),
                   requestProperty(e->atom, XCB_ATOM_CARDINAL));
    } else if (e->atom == ATOM(_MEEGOTOUCH_VIDEO_ALPHA)) {
        addRequest(videoGlobalAlphaKey, SLOT(videoGlobalAlpha()),
                   requestProperty(e->atom, XCB_ATOM_CARDINAL));
    } else if (e->atom == ATOM(_MEEGOTOUCH_DECORATOR_WINDOW)) {
        addRequest(isDecoratorKey, SLOT(isDecorator()),
                   requestProperty(MCompAtoms::_MEEGOTOUCH_DECORATOR_WINDOW,
                                   XCB_ATOM_CARDINAL));
        return true;
    } else if (e->atom == ATOM(_MEEGOTOUCH_ORIENTATION_ANGLE)) {
        addRequest(orientationAngleKey, SLOT(orientationAngle()),
              requestProperty(MCompAtoms::_MEEGOTOUCH_ORIENTATION_ANGLE,
                              XCB_ATOM_CARDINAL));
    } else if (e->atom == ATOM(_MEEGOTOUCH_MSTATUSBAR_GEOMETRY)) {
        addRequest(statusbarGeometryKey, SLOT(statusbarGeometry()),
            requestProperty(e->atom, XCB_ATOM_CARDINAL, 4));
        return true; // re-check _MEEGOTOUCH_STATUSBAR_VISIBLE
    } else if (e->atom == ATOM(WM_PROTOCOLS)) {
        addRequest(supportedProtocolsKey, SLOT(supportedProtocols()),
                   requestProperty(e->atom, XCB_ATOM_ATOM, 100));
        return true;
    } else if (e->atom == ATOM(_NET_WM_STATE)) {
        addRequest(netWmStateKey, SLOT(netWmState()),
                   requestProperty(e->atom, XCB_ATOM_ATOM, 100));
        return false;
    } else if (e->atom == ATOM(WM_STATE)) {
        addRequest(windowStateKey, SLOT(windowState()),
                   requestProperty(e->atom, ATOM(WM_STATE)));
        return true;
    } else if (e->atom == ATOM(_MEEGO_STACKING_LAYER)) {
        addRequest(meegoStackingLayerKey, SLOT(meegoStackingLayer()),
                   requestProperty(e->atom, XCB_ATOM_CARDINAL));
        if (window_state == NormalState) {
            // raise it so that it becomes on top of same-leveled windows
            MCompositeManager *m = (MCompositeManager*)qApp;
            m->d->positionWindow(window, true);
        }
        return true;
    } else if (e->atom == ATOM(_MEEGO_LOW_POWER_MODE)) {
        addRequest(lowPowerModeKey, SLOT(lowPowerMode()),
                   requestProperty(e->atom, XCB_ATOM_CARDINAL));
    } else if (e->atom == ATOM(_MEEGOTOUCH_OPAQUE_WINDOW)) {
        addRequest(opaqueWindowKey, SLOT(opaqueWindow()),
                   requestProperty(e->atom, XCB_ATOM_CARDINAL));
        return true;  // check if compositing mode needs to change
    } else if (e->atom == ATOM(_MEEGOTOUCH_CUSTOM_REGION)) {
        emit customRegionChanged(this);
    } else if (e->atom == ATOM(WM_NAME)) {
        addRequest(wmNameKey, SLOT(wmName()),
                   requestProperty(MCompAtoms::WM_NAME, XCB_ATOM_STRING, 100));
    } else if (e->atom == ATOM(_MEEGOTOUCH_NO_ANIMATIONS)) {
        addRequest(noAnimationsKey, SLOT(noAnimations()),
               requestProperty(MCompAtoms::_MEEGOTOUCH_NO_ANIMATIONS,
                               XCB_ATOM_CARDINAL));
    } else if (e->atom == ATOM(_OMAP_VIDEO_OVERLAY)) {
        addRequest(videoOverlayKey, SLOT(videoOverlay()),
                   requestProperty(MCompAtoms::_OMAP_VIDEO_OVERLAY,
                                   XCB_ATOM_INTEGER));
    } else if (e->atom == ATOM(_NET_WM_PID)) {
        wm_pid = 0;
        if (e->state == PropertyNewValue)
            addRequest(pidKey, SLOT(pid()),
                       requestProperty(MCompAtoms::_NET_WM_PID,
                                       XCB_ATOM_CARDINAL));
    } else if (e->state == PropertyNewValue
               && e->atom == ATOM(_MEEGOTOUCH_PRESTARTED)) {
        prestarted = true;
        wmhints->flags &= ~StateHint;
    }
    return false;
}

unsigned MWindowPropertyCache::pid()
{
    CARD32 val;
    if (is_valid && getCARD32(pidKey, &val))
        wm_pid = val;
    return wm_pid;
}

int MWindowPropertyCache::windowState()
{
    const CollectorKey me = windowStateKey;
    if (requests[me].requested && requests[me].cookie) {
        xcb_generic_error_t *error = 0;

        xcb_get_property_reply_t *r;
        xcb_get_property_cookie_t c = { requests[me].cookie };
        r = xcb_get_property_reply(xcb_conn, c, &error);
        replyCollected(me);
        if (r && xcb_get_property_value_length(r) >= int(sizeof(CARD32)))
            window_state = ((CARD32*)xcb_get_property_value(r))[0];
        else if (window_state < 0 || window_state == WithdrawnState
                 || error) {
            // Qt deleted WM_STATE, but we don't mind.  Mark it so
            // that MCompositeManagerPrivate::setWindowState() will set it.
            window_state = -1;
            free(error);
        } else // Ei, vittu!
            // We have an idea about this window's state and Qt is trying
            // to interfere because it doesn't know yet that we are in
            // command.  Don't let this happen.
            XChangeProperty(QX11Info::display(), window,
                            ATOM(WM_STATE), ATOM(WM_STATE),
                            32, PropModeReplace,
                            (unsigned char *)(CARD32[]){window_state, None},
                            2);
        free(r);
    }
    return window_state;
}

void MWindowPropertyCache::setWindowState(int state)
{
    // The window's type is about to change.  Change the the idea of the
    // property cache about the window's type now to make windowState()
    // non-blocking.
    cancelRequest(windowStateKey);
    window_state = state;
}

unsigned MWindowPropertyCache::orientationAngle()
{
    CARD32 val;
    if (is_valid && getCARD32(orientationAngleKey, &val))
        orientation_angle = val;
    return orientation_angle;
}

const QRect &MWindowPropertyCache::statusbarGeometry()
{
    const CollectorKey me = statusbarGeometryKey;
    if (!is_valid || !requests[me].requested || !requests[me].cookie)
        return statusbar_geom;

    xcb_get_property_reply_t *r;
    xcb_get_property_cookie_t c = { requests[me].cookie };
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
    const CollectorKey me = supportedProtocolsKey;
    if (!is_valid || !requests[me].requested || !requests[me].cookie)
        return wm_protocols;

    xcb_get_property_reply_t *r;
    xcb_get_property_cookie_t c = { requests[me].cookie };
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

const QVector<Atom> &MWindowPropertyCache::netWmState()
{
    const CollectorKey me = netWmStateKey;
    if (!is_valid || !requests[me].requested || !requests[me].cookie)
        return net_wm_state;

    xcb_get_property_reply_t *r;
    xcb_get_property_cookie_t c = { requests[me].cookie };
    r = xcb_get_property_reply(xcb_conn, c, 0);
    replyCollected(me);
    if (!r) {
        net_wm_state.clear();
        return net_wm_state;
    }
    int n_atoms = xcb_get_property_value_length(r) / sizeof(Atom);
    Atom* atoms = (Atom*)xcb_get_property_value(r);
    net_wm_state.resize(n_atoms);
    for (int i = 0; i < n_atoms; ++i)
        net_wm_state[i] = atoms[i];
    free(r);
    return net_wm_state;
}

bool MWindowPropertyCache::addToNetWmState(Atom state)
{
    if (!is_valid || is_virtual)
        return false;

    if (force_skipping_taskbar
        && state == ATOM(_NET_WM_STATE_SKIP_TASKBAR))
        // don't restore @was_skipping_taskbar whatever it was,
        // @state is the new setting
        force_skipping_taskbar = false;

    // netWmState() will complete a pending request
    if (netWmState().contains(state))
        return false;
    Q_ASSERT(!requests[netWmStateKey].requested
             || !requests[netWmStateKey].cookie);

    net_wm_state.append(state);
    XChangeProperty(QX11Info::display(), window,
                    ATOM(_NET_WM_STATE), XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)net_wm_state.constData(),
                    net_wm_state.count());
    return true;
}

bool MWindowPropertyCache::removeFromNetWmState(Atom state)
{
    if (!is_valid || is_virtual)
        return false;

    if (force_skipping_taskbar
        && state == ATOM(_NET_WM_STATE_SKIP_TASKBAR))
        force_skipping_taskbar = false;
    netWmState(); // update @net_wm_state

    // remove all occurrances of @state from @net_wm_state
    bool state_changed = false;
    int i = 0, nstates = net_wm_state.count();
    while (i < nstates)
        if (net_wm_state[i] == state) {
            state_changed = true;
            net_wm_state.remove(i);
            nstates--;
        } else
            i++;
    if (!state_changed)
        // didn't actually remove anything
        return false;

    Q_ASSERT(!requests[netWmStateKey].requested
             || !requests[netWmStateKey].cookie);
    XChangeProperty(QX11Info::display(), window,
                    ATOM(_NET_WM_STATE), XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)net_wm_state.constData(),
                    net_wm_state.count());
    return true;
}

void MWindowPropertyCache::forceSkippingTaskbar(bool force)
{
    Atom skipata = ATOM(_NET_WM_STATE_SKIP_TASKBAR);
    if (force) {
        if (!force_skipping_taskbar) {
            // !force => force, remember whether it @was_skipping_taskbar
            was_skipping_taskbar = !addToNetWmState(skipata);
            if (!was_skipping_taskbar) {
                // we added _NET_WM_STATE_SKIP_TASKBAR there, add a marker
                // so that we can clean it up if we restart
                setSkippingTaskbarMarker(true);
            }
        } else // just reinforce the state
            addToNetWmState(skipata);
    } else {
        if (force_skipping_taskbar && !was_skipping_taskbar) {
            // force => !force, restore the state
            removeFromNetWmState(skipata);
            setSkippingTaskbarMarker(false);
        }
    }
    force_skipping_taskbar = force;
}

bool MWindowPropertyCache::skippingTaskbarMarker()
{
    CARD32 val;
    if (is_valid && getCARD32(skippingTaskbarMarkerKey, &val))
        skipping_taskbar_marker = !!val;
    return skipping_taskbar_marker;
}

void MWindowPropertyCache::setSkippingTaskbarMarker(bool setting)
{
    if (skippingTaskbarMarker() == setting)
        return;
    if (setting) {
        const long value = 1;
        XChangeProperty(QX11Info::display(), window,
                        ATOM(_MCOMPOSITOR_SKIP_TASKBAR),
                        XA_CARDINAL, 32, PropModeReplace,
                        (unsigned char *)&value, 1);
    } else {
        XDeleteProperty(QX11Info::display(), window,
                        ATOM(_MCOMPOSITOR_SKIP_TASKBAR));
    }
    skipping_taskbar_marker = setting;
}

const QRectF &MWindowPropertyCache::iconGeometry()
{
    const CollectorKey me = iconGeometryKey;
    if (!is_valid || !requests[me].requested || !requests[me].cookie)
        return icon_geometry;

    xcb_get_property_reply_t *r;
    xcb_get_property_cookie_t c = { requests[me].cookie };
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

int MWindowPropertyCache::globalAlpha()
{
    CARD32 val;
    if (is_valid && getCARD32(globalAlphaKey, &val))
        /* Map 0..0xFFFFFFFF -> 0..0xFF. */
        global_alpha = val == 0 ? 255 : val >> 24;
    return global_alpha;
}

int MWindowPropertyCache::videoGlobalAlpha()
{    
    CARD32 val;
    if (is_valid && getCARD32(videoGlobalAlphaKey, &val))
        /* Map 0..0xFFFFFFFF -> 0..0xFF. */
        video_global_alpha = val == 0 ? 255 : val >> 24;
    return video_global_alpha;
}

Atom MWindowPropertyCache::windowTypeAtom()
{
    const CollectorKey me = windowTypeAtomKey;
    if (!is_valid)
        return None;
    if (!requests[me].requested || !requests[me].cookie) {
        Q_ASSERT(!type_atoms.isEmpty());
        return type_atoms[0];
    }

    type_atoms.resize(0);
    xcb_get_property_reply_t *r;
    xcb_get_property_cookie_t c = { requests[me].cookie };
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
             type_atom == ATOM(_NET_WM_WINDOW_TYPE_MENU)) {
        if (type_atoms.contains(ATOM(_MEEGOTOUCH_NET_WM_WINDOW_TYPE_SHEET)))
            window_type = MCompAtoms::SHEET;
        else
            window_type = MCompAtoms::FRAMELESS;
    } else if (transientFor())
        window_type = MCompAtoms::UNKNOWN;
    else // fdo spec suggests unknown non-transients must be normal
        window_type = MCompAtoms::NORMAL;
    return window_type;
}

void MWindowPropertyCache::setRealGeometry(const QRect &rect)
{
    const CollectorKey me = realGeometryKey;
    if (!is_valid)
        return;

    cancelRequest(me);
    real_geom = rect;

    // shape needs to be refreshed in case it was the default value
    // (i.e. the same as geometry), because there is no ShapeNotify
    if (!isUpdate(shapeRegionKey) || QRegion(real_geom) != shape_region)
        shapeRefresh();
}

const QRect MWindowPropertyCache::realGeometry()
{
    const CollectorKey me = realGeometryKey;
    if (is_valid && requests[me].requested && requests[me].cookie) {
        xcb_get_geometry_reply_t *xcb_real_geom;
        xcb_get_geometry_cookie_t c = { requests[me].cookie };
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
    const CollectorKey me = wmNameKey;
    if (is_valid && requests[me].requested && requests[me].cookie) {
        xcb_get_property_reply_t *r;
        xcb_get_property_cookie_t c = { requests[me].cookie };
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
    addRequest(shapeRegionKey, SLOT(shapeRegion()),
               xcb_shape_get_rectangles(xcb_conn, window,
                                        ShapeBounding).sequence);
}

bool MWindowPropertyCache::readSplashProperty(Window win,
                    unsigned &splash_pid, unsigned &splash_pixmap,
                    QString &splash_landscape, QString &splash_portrait)
{
    xcb_get_property_reply_t *r;
    xcb_get_property_cookie_t c;
    bool ret = false;
    c = xcb_get_property(xcb_conn, 0, win,
                         ATOM(_MEEGO_SPLASH_SCREEN),
                         XCB_ATOM_STRING, 0, 1000);
    r = xcb_get_property_reply(xcb_conn, c, 0);
    if (r) {
        int len = xcb_get_property_value_length(r);
        if (len >= 4 + 3 /* 4 separators + 3 non-empty items */) {
            QByteArray a((const char*)xcb_get_property_value(r), len);
            QList<QByteArray> l = a.split('\0');
            if (l.size() < 5)
                qWarning("_MEEGO_SPLASH_SCREEN has only %d items", l.size());
            else {
                splash_pid = l.at(0).toUInt();
                splash_portrait = l.at(2).data();
                splash_landscape = l.at(3).data();
                splash_pixmap = l.at(4).toUInt();
                ret = true;
            }
        }
        free(r);
    }
    return ret;
}

void MWindowPropertyCache::damageTracking(bool enabled)
{
    if (!is_valid || is_virtual)
        return;
    if (enabled) {
        if (!damage_object && !isInputOnly()) {
            damage_report_level = XDamageReportNonEmpty;
            damage_object = XDamageCreate(QX11Info::display(), window,
                                          damage_report_level);
        }
    } else if (damage_object) {
        XDamageDestroy(QX11Info::display(), damage_object);
        damage_object = 0;
        damage_report_level = -1;
        pending_damage = false;
    }
}

void MWindowPropertyCache::damageTracking(int damageReportLevel)
{
    if (!is_valid || is_virtual || isInputOnly())
        return;
    else if (damage_object && damageReportLevel == damage_report_level)
        return;
    else if (damage_object)
        XDamageDestroy(QX11Info::display(), damage_object);

    damage_object = XDamageCreate(QX11Info::display(), window,
                                  damageReportLevel);
    damage_report_level = damageReportLevel;
}

void MWindowPropertyCache::damageSubtract()
{
    if (damage_object && damage_report_level == XDamageReportNonEmpty)
        XDamageSubtract(QX11Info::display(), damage_object, None, None);
    pending_damage = false;
}

void MWindowPropertyCache::damageReceived()
{
    if (damage_object) // <-- check object for unit tests
        pending_damage = true;
}

int MWindowPropertyCache::waitingForDamage() const
{
    return waiting_for_damage;
}

void MWindowPropertyCache::setWaitingForDamage(int waiting)
{
    if (damage_object && waiting == 0 &&
            damage_report_level == XDamageReportRawRectangles) {
        // recreate the damage object to switch from
        // XDamageReportRawRectangles to XDamageReportNonEmpty
        damageTracking(XDamageReportNonEmpty);
    }
    waiting_for_damage = waiting;
}

bool MWindowPropertyCache::isAppWindow(bool include_transients)
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    if (!include_transients && p->d->getLastVisibleParent(this))
        return false;

    if (!isOverrideRedirect() && !isDecorator() &&
        (windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_NORMAL) ||
         windowTypeAtom() == ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE) ||
         /* non-modal, non-transient dialogs behave like applications */
         (windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DIALOG) &&
          !netWmState().contains(ATOM(_NET_WM_STATE_MODAL)) &&
          !p->d->getLastVisibleParent(this))))
        return true;
    return false;
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
