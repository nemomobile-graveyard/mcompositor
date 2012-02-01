#include <QtTest/QtTest>
#include <QtGui>
#include <QGLWidget>
#include <mcompositemanager.h>
#include <mcompositemanager_p.h>
#include <mwindowpropertycache.h>
#include <mcompositewindow.h>
#include <mtexturepixmapitem.h>
#include <mcompositewindowanimation.h>
#include <mdevicestate.h>
#include "ut_compositing.h"

#include <QtDebug>

#include <X11/Xlib.h>

// fake windows should be less than the root window's value
#define VKB_1 50
#define VKB_2 60

static int dheight, dwidth;

static QHash<Drawable,int> lastDamageReportLevel;
static QHash<Drawable,int> damageCreationCounter;
Damage
XDamageCreate (Display*, Drawable d, int level)
{
    lastDamageReportLevel[d] = level;
    ++damageCreationCounter[d];
    return 1;
}

// Skip bad window messages for mock windows
static int error_handler(Display * , XErrorEvent *)
{    
    return 0;
}

static Drawable request_testpixmap()
{
    QPixmap* p = new QPixmap(1,1);
    p->fill();
    return p->handle();
}

class fake_LMT_window : public MWindowPropertyCache
{
public:
    fake_LMT_window(Window w, unsigned width = dwidth,
                    unsigned height = dheight)
        : MWindowPropertyCache(w, &attrs)
    {
        cancelAllRequests();
        window = w;
        memset(&attrs, 0, sizeof(attrs));
        setIsMapped(false);
        setRealGeometry(QRect(0, 0, width, height));
        // icon geometry can be required for iconifying animation
        icon_geometry = QRect(0, 0, dwidth / 2, dheight / 2);
        type_atoms.append(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
        window_state = NormalState;
        window_type = MCompAtoms::FRAMELESS;
        has_alpha = 0;
        // mark valid to create animation object
        is_valid = true;
    }
    void prependType(Atom a) { type_atoms.prepend(a); }
    void setTransientFor(Window w) { transient_for = w; }
    void addToTransients(Window w) { transients.append(w); }
    void setAlpha(bool b) { has_alpha = b; }
    Damage damageObject() const { return damage_object; }
    bool pendingDamage() { return pending_damage; }

    xcb_get_window_attributes_reply_t attrs;

    friend class ut_Stacking;
};

class fake_desktop_window : public MWindowPropertyCache
{
public:
    fake_desktop_window(Window w)
        : MWindowPropertyCache(w, &attrs)
    {
        cancelAllRequests();
        window = w;
        memset(&attrs, 0, sizeof(attrs));
        setIsMapped(true);
        setRealGeometry(QRect(0, 0, dwidth, dheight));
        type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_DESKTOP));
        type_atoms.append(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
        window_state = NormalState;
        window_type = MCompAtoms::DESKTOP;
        has_alpha = 0;
        // mark valid to create animation object
        is_valid = true;
    }
    Damage damageObject() const { return damage_object; }

    xcb_get_window_attributes_reply_t attrs;
};

class fake_device_state : public MDeviceState
{
public:
    fake_device_state() : fake_display_off(false) {}
    bool displayOff() const { return fake_display_off; }
    bool fake_display_off;
    const QString &touchScreenLock() const { return fake_touchScreenLockMode; }
    QString fake_touchScreenLockMode;
};

static fake_device_state *device_state;

void ut_Compositing::initTestCase()
{
    cmgr = (MCompositeManager*)qApp;
    cmgr->setSurfaceWindow(0);
    cmgr->d->prepare();
    cmgr->d->xserver_stacking.init();

    // create an altered MDeviceState
    device_state = new fake_device_state();
    delete cmgr->d->device_state;
    cmgr->d->device_state = device_state;
    device_state->fake_touchScreenLockMode = "unlocked";

    QVector<MWindowPropertyCache *> empty;
    prepareStack(empty);
}

void ut_Compositing::prepareStack(QVector<MWindowPropertyCache *> &t)
{
    cmgr->d->stacking_list.clear();
    cmgr->d->prop_caches.clear();
    for (int i = 0; i < t.size(); ++i) {
        cmgr->d->stacking_list.append(t[i]->winId());
        cmgr->d->prop_caches[t[i]->winId()] = t[i];
        if (t[i]->windowType() == MCompAtoms::DESKTOP)
            cmgr->d->desktop_window = t[i]->winId();
    }
    cmgr->d->xserver_stacking.setState(cmgr->d->stacking_list.toVector());
}

void ut_Compositing::mapWindow(MWindowPropertyCache *pc)
{
    if (!cmgr->d->prop_caches.contains(pc->winId()))
        cmgr->d->xserver_stacking.windowCreated(pc->winId());
    cmgr->d->prop_caches[pc->winId()] = pc;

    XMapRequestEvent mre;
    memset(&mre, 0, sizeof(mre));
    mre.window = pc->winId();
    mre.parent = QX11Info::appRootWindow();
    cmgr->d->mapRequestEvent(&mre);

    XMapEvent e;
    memset(&e, 0, sizeof(e));
    e.window = pc->winId();
    e.event = QX11Info::appRootWindow();
    cmgr->d->mapEvent(&e);
}

void ut_Compositing::unmapWindow(MWindowPropertyCache *pc)
{
    XUnmapEvent e;
    memset(&e, 0, sizeof(e));
    e.window = pc->winId();
    e.event = QX11Info::appRootWindow();
    cmgr->d->unmapEvent(&e);
}

void ut_Compositing::fakeDamageEvent(MCompositeWindow *cw)
{
    XDamageNotifyEvent e;
    memset(&e, 0, sizeof(e));
    e.drawable = cw->window();
    cmgr->d->damageEvent(&e);
}

void ut_Compositing::testDesktopMapping()
{
    lastDamageReportLevel[1] = -1;
    damageCreationCounter[1] = 0;
    fake_desktop_window *desk = new fake_desktop_window(1);
    mapWindow(desk);
    MCompositeWindow *w = cmgr->d->windows.value(1, 0);
    QCOMPARE(w != 0, true);
    QCOMPARE(lastDamageReportLevel[1], XDamageReportRawRectangles);
    QCOMPARE(desk->damage_report_level, XDamageReportRawRectangles);
    QCOMPARE(damageCreationCounter[1], 1);
    fakeDamageEvent(w);
    QCOMPARE(damageCreationCounter[1], 2);
    QCOMPARE(lastDamageReportLevel[1], XDamageReportNonEmpty);
    QCOMPARE(desk->damage_report_level, XDamageReportNonEmpty);
    fakeDamageEvent(w);
    QCOMPARE(damageCreationCounter[1], 2);
    QCOMPARE(desk->damage_report_level, XDamageReportNonEmpty);

    QCOMPARE(!cmgr->d->stacking_list.isEmpty(), true);
    QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), true);
    QCOMPARE(desk->damageObject() == 0, true);
    QCOMPARE(cmgr->d->compositing, false);
    QCOMPARE(cmgr->d->overlay_mapped, false);
    QCOMPARE(w->window() == 1, true);
    QCOMPARE(((MTexturePixmapItem*)w)->isDirectRendered(), true);
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
    QCOMPARE(damageCreationCounter[1], 2);
}

void ut_Compositing::testAppMapping()
{
    fake_LMT_window *app = new fake_LMT_window(2);
    lastDamageReportLevel[2] = -1;
    damageCreationCounter[2] = 0;
    mapWindow(app);
    MCompositeWindow *w = cmgr->d->windows.value(2, 0);
    QCOMPARE(w != 0, true);

    // check that it is not visible after idle handlers
    QTest::qWait(10);
    QCOMPARE(app->damageObject() != 0, true);
    QCOMPARE(damageCreationCounter[2], 1);
    QCOMPARE(lastDamageReportLevel[2], XDamageReportRawRectangles);
    QCOMPARE(app->damage_report_level, XDamageReportRawRectangles);
    QCOMPARE(w->isVisible(), false);
    QCOMPARE(w->windowObscured(), false);

    fakeDamageEvent(w);
    QCOMPARE(damageCreationCounter[2], 1);
    QCOMPARE(lastDamageReportLevel[2], XDamageReportRawRectangles);
    QCOMPARE(app->damage_report_level, XDamageReportRawRectangles);
    fakeDamageEvent(w);
    QCOMPARE(damageCreationCounter[2], 2);
    QCOMPARE(lastDamageReportLevel[2], XDamageReportNonEmpty);
    QCOMPARE(app->damage_report_level, XDamageReportNonEmpty);
    QCOMPARE(w->windowAnimator()->isActive(), true);
    while (w->windowAnimator()->isActive()) {
        QCOMPARE(cmgr->d->compositing, true);
        QCOMPARE(cmgr->d->overlay_mapped, true);
        QCOMPARE(((MTexturePixmapItem*)w)->isDirectRendered(), false);
        QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), false);
        QTest::qWait(500); // wait the animation to finish
    }
    QCOMPARE(w->isVisible(), true);
    QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), true);
    QCOMPARE(app->damageObject() == 0, true);
    QCOMPARE(cmgr->d->compositing, false);
    QCOMPARE(cmgr->d->overlay_mapped, false);
    QCOMPARE(((MTexturePixmapItem*)w)->isDirectRendered(), true);
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
    QCOMPARE(damageCreationCounter[2], 2);
}

// unmap an application (depends on the previous test)
void ut_Compositing::testAppUnmapping()
{
    lastDamageReportLevel[2] = -1;
    damageCreationCounter[2] = 0;
    MCompositeWindow *w = cmgr->d->windows.value(2, 0);
    QCOMPARE(w != 0, true);
    ((MTexturePixmapItem*)w)->d->TFP.drawable = request_testpixmap();
    w->closeWindowRequest();
    unmapWindow(w->propertyCache());
    QCOMPARE(damageCreationCounter[2], 1);
    QCOMPARE(lastDamageReportLevel[2], XDamageReportNonEmpty);
    QCOMPARE(w->propertyCache()->damage_report_level, XDamageReportNonEmpty);
    QCOMPARE(w->windowAnimator()->isActive(), true);
    while (w->windowAnimator()->isActive()) {
        QCOMPARE(cmgr->d->compositing, true);
        QCOMPARE(cmgr->d->overlay_mapped, true);
        QCOMPARE(((MTexturePixmapItem*)w)->isDirectRendered(), false);
        QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), false);
        QTest::qWait(500); // wait the animation to finish
    }
    QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), true);
    // currently we have damage object after the window was unmapped since
    // we could not figure out a good reason why not have it...
    fake_LMT_window *app = (fake_LMT_window*)cmgr->d->prop_caches.value(2, 0);
    QCOMPARE(app->damageObject() != 0, true);
    QCOMPARE(cmgr->d->compositing, false);
    QCOMPARE(cmgr->d->overlay_mapped, false);
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
}

// test re-mapping of an unmapped app (depends on the previous test)
void ut_Compositing::testAppRemapping()
{
    lastDamageReportLevel[2] = -1;
    damageCreationCounter[2] = 0;
    MCompositeWindow *w = cmgr->d->windows.value(2, 0);
    fake_LMT_window *app = (fake_LMT_window*)cmgr->d->prop_caches.value(2, 0);
    QCOMPARE(w != 0, true);
    QCOMPARE(w->isMapped(), false);
    mapWindow(w->propertyCache());

    // check that it is not visible after idle handlers
    QTest::qWait(10);
    QCOMPARE(damageCreationCounter[2], 1);
    QCOMPARE(lastDamageReportLevel[2], XDamageReportRawRectangles);
    QCOMPARE(app->damage_report_level, XDamageReportRawRectangles);
    QCOMPARE(app->damageObject() != 0, true);
    QCOMPARE(w->isVisible(), false);
    QCOMPARE(w->windowObscured(), false);
    QCOMPARE(((MTexturePixmapItem*)w)->isDirectRendered(), false);

    fakeDamageEvent(w);
    QCOMPARE(damageCreationCounter[2], 1);
    QCOMPARE(lastDamageReportLevel[2], XDamageReportRawRectangles);
    QCOMPARE(app->damage_report_level, XDamageReportRawRectangles);
    fakeDamageEvent(w);
    QCOMPARE(damageCreationCounter[2], 2);
    QCOMPARE(lastDamageReportLevel[2], XDamageReportNonEmpty);
    QCOMPARE(app->damage_report_level, XDamageReportNonEmpty);

    QCOMPARE(w->windowAnimator()->isActive(), true);
    while (w->windowAnimator()->isActive()) {
        QCOMPARE(cmgr->d->compositing, true);
        QCOMPARE(cmgr->d->overlay_mapped, true);
        QCOMPARE(((MTexturePixmapItem*)w)->isDirectRendered(), false);
        QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), false);
        QTest::qWait(500); // wait the animation to finish
    }
    QCOMPARE(w->isVisible(), true);
    QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), true);
    QCOMPARE(app->damageObject() == 0, true);
    QCOMPARE(cmgr->d->compositing, false);
    QCOMPARE(cmgr->d->overlay_mapped, false);
    QCOMPARE(((MTexturePixmapItem*)w)->isDirectRendered(), true);
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
    QCOMPARE(damageCreationCounter[2], 2);
    QCOMPARE(app->damage_report_level, -1);
}

// VKB appearing for an app when that app is showing the mapping animation
void ut_Compositing::testVkbMappingWhenAppAnimating()
{
    fake_LMT_window *app = new fake_LMT_window(3);
    mapWindow(app);
    MCompositeWindow *w = cmgr->d->windows.value(3, 0);
    QCOMPARE(w != 0, true);
    QCOMPARE(app->damageObject() != 0, true);

    w->damageReceived();
    w->damageReceived();
    QTest::qWait(10); // get the animation started
    QCOMPARE(w->windowAnimator()->isActive(), true);

    MCompositeWindow *v;
    fake_LMT_window *vkb;
    if (w->windowAnimator()->isActive()) {
        vkb = new fake_LMT_window(VKB_1);
        vkb->prependType(ATOM(_NET_WM_WINDOW_TYPE_INPUT));
        // VKB mapped during the animation
        vkb->setTransientFor(3);
        app->addToTransients(VKB_1);
        mapWindow(vkb);
        v = cmgr->d->windows.value(VKB_1, 0);
        QCOMPARE(v != 0, true);
        QCOMPARE(v->propertyCache()->windowTypeAtom()
                 == ATOM(_NET_WM_WINDOW_TYPE_INPUT), true);
        QCOMPARE(v->isVisible(), false);
        QCOMPARE(v->windowObscured(), false);
        QCOMPARE(v->paintedAfterMapping(), false);
        QCOMPARE(vkb->damageObject() != 0, true);

        QCOMPARE(cmgr->d->compositing, true);
        QCOMPARE(cmgr->d->overlay_mapped, true);
        QCOMPARE(((MTexturePixmapItem*)w)->isDirectRendered(), false);
        QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), false);
        QTest::qWait(1000); // wait the animation to finish

        QCOMPARE(v->isVisible(), true);
        QCOMPARE(v->windowObscured(), false);
        QCOMPARE(v->paintedAfterMapping(), true);
        // self-compositing VKB requires unobscured app
        QCOMPARE(w->windowObscured(), false);
    } else
        QCOMPARE(false, true); // fail: animation did not start
    QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), true);
    // FIXME: app has the damage object even though we don't need it
    QCOMPARE(app->damageObject() != 0, true);
    QCOMPARE(vkb->damageObject() == 0, true);
    QCOMPARE(cmgr->d->compositing, false);
    QCOMPARE(cmgr->d->overlay_mapped, false);
    QCOMPARE(((MTexturePixmapItem*)v)->isDirectRendered(), true);
    // self-compositing VKB requires redirected app
    QCOMPARE(((MTexturePixmapItem*)w)->isDirectRendered(), false);
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
}

// normal VKB mapping case
void ut_Compositing::testVkbMapping()
{
    fake_LMT_window *app = new fake_LMT_window(4);
    mapWindow(app);
    MCompositeWindow *w = cmgr->d->windows.value(4, 0);
    QCOMPARE(w != 0, true);

    fakeDamageEvent(w);
    fakeDamageEvent(w);
    QCOMPARE(w->windowAnimator()->isActive(), true);
    while (w->windowAnimator()->isActive())
        QTest::qWait(500); // wait the animation to finish

    MCompositeWindow *v;
    fake_LMT_window *vkb = new fake_LMT_window(VKB_2);
    vkb->prependType(ATOM(_NET_WM_WINDOW_TYPE_INPUT));
    // VKB mapped
    vkb->setTransientFor(4);
    app->addToTransients(VKB_2);
    mapWindow(vkb);
    QTest::qWait(10); // run the idle handlers
    v = cmgr->d->windows.value(VKB_2, 0);
    QCOMPARE(v != 0, true);
    QCOMPARE(v->propertyCache()->windowTypeAtom()
                 == ATOM(_NET_WM_WINDOW_TYPE_INPUT), true);
    v->damageReceived();
    v->damageReceived();
    QCOMPARE(v->isVisible(), true);
    QCOMPARE(v->windowObscured(), false);
    QCOMPARE(v->paintedAfterMapping(), true);
    QCOMPARE(((MTexturePixmapItem*)v)->isDirectRendered(), true);

    QCOMPARE(cmgr->d->compositing, false);
    QCOMPARE(cmgr->d->overlay_mapped, false);
    QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), true);
    // self-compositing VKB requires unobscured and redirected app
    QCOMPARE(w->windowObscured(), false);
    QCOMPARE(((MTexturePixmapItem*)w)->isDirectRendered(), false);
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
    // FIXME: app has the damage object even though we don't need it
    QCOMPARE(app->damageObject() != 0, true);
    QCOMPARE(vkb->damageObject() == 0, true);
    // check that damage of the app is not handled
    fakeDamageEvent(w);
    QCOMPARE(app->pendingDamage(), true);
}

// transparent banner mapping case (depends on the previous test)
void ut_Compositing::testBannerMapping()
{
    fake_LMT_window *banner = new fake_LMT_window(5);
    banner->prependType(ATOM(_NET_WM_WINDOW_TYPE_NOTIFICATION));
    banner->setAlpha(true);
    mapWindow(banner);
    QTest::qWait(10); // run the idle handlers
    MCompositeWindow *w = cmgr->d->windows.value(5, 0);
    QCOMPARE(w != 0, true);
    // we wait for damage events
    QCOMPARE(w->isVisible(), false);
    QCOMPARE(w->windowObscured(), false);
    QCOMPARE(w->paintedAfterMapping(), false);
    QCOMPARE(banner->damageObject() != 0, true);
    QCOMPARE(((MTexturePixmapItem*)w)->isDirectRendered(), false);
    QCOMPARE(cmgr->d->compositing, true);
    QCOMPARE(cmgr->d->overlay_mapped, true);
    QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), false);
    // check that damage is handled
    for (int i = 0; i < 100; ++i) {
        fakeDamageEvent(w);
        QCOMPARE(banner->pendingDamage(), false);
    }

    // check the windows below
    MCompositeWindow *vkb = cmgr->d->windows.value(VKB_2, 0);
    QCOMPARE(vkb->isVisible(), true);
    QCOMPARE(vkb->windowObscured(), false);
    QCOMPARE(vkb->paintedAfterMapping(), true);
    QCOMPARE(((MTexturePixmapItem*)vkb)->isDirectRendered(), false);
    fake_LMT_window *pc = (fake_LMT_window*)cmgr->d->prop_caches.value(VKB_2, 0);
    QCOMPARE(pc->damageObject() != 0, true);
    // check that damage is handled
    for (int i = 0; i < 100; ++i) {
        fakeDamageEvent(vkb);
        QCOMPARE(pc->pendingDamage(), false);
    }
    // self-compositing VKB requires unobscured and redirected app
    MCompositeWindow *app = cmgr->d->windows.value(4, 0);
    QCOMPARE(app->windowObscured(), false);
    QCOMPARE(((MTexturePixmapItem*)app)->isDirectRendered(), false);
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
    // check that damage of the app is not handled
    fakeDamageEvent(app);
    QCOMPARE(((fake_LMT_window*)app->propertyCache())->pendingDamage(), true);
}

// transparent banner unmapping case (depends on the previous test)
void ut_Compositing::testBannerUnmapping()
{
    MCompositeWindow *banner = cmgr->d->windows.value(5, 0);
    QCOMPARE(banner != 0, true);
    unmapWindow(banner->propertyCache());
    QTest::qWait(500); // run the idle handlers

    QCOMPARE(cmgr->d->compositing, false);
    QCOMPARE(cmgr->d->overlay_mapped, false);
    QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), true);

    // check the windows below
    MCompositeWindow *vkb = cmgr->d->windows.value(VKB_2, 0);
    QCOMPARE(vkb->isVisible(), true);
    QCOMPARE(vkb->windowObscured(), false);
    QCOMPARE(vkb->paintedAfterMapping(), true);
    QCOMPARE(((MTexturePixmapItem*)vkb)->isDirectRendered(), true);
    fake_LMT_window *pc = (fake_LMT_window*)cmgr->d->prop_caches.value(VKB_2, 0);
    QCOMPARE(pc->damageObject() == 0, true);
    // self-compositing VKB requires unobscured and redirected app
    MCompositeWindow *app = cmgr->d->windows.value(4, 0);
    QCOMPARE(app->windowObscured(), false);
    QCOMPARE(((MTexturePixmapItem*)app)->isDirectRendered(), false);
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
}

// test that damage that arrived when display is off is handled after
// display is turned on
void ut_Compositing::testDamageDuringDisplayOff()
{
    fake_LMT_window *rgba_app = new fake_LMT_window(6);
    rgba_app->setAlpha(true);
    mapWindow(rgba_app);
    QTest::qWait(10); // run the idle handlers
    MCompositeWindow *cw = cmgr->d->windows.value(6, 0);

    fakeDamageEvent(cw);
    QCOMPARE(rgba_app->pendingDamage(), false);
    fakeDamageEvent(cw);
    QCOMPARE(rgba_app->pendingDamage(), false);
    QCOMPARE(rgba_app->damageObject() != 0, true);
    while (cw->windowAnimator()->isActive())
        QTest::qWait(500); // wait the animation to finish

    // display off
    device_state->fake_display_off = true;
    cmgr->d->displayOff(true);
    QCOMPARE(cmgr->d->device_state->displayOff(), true);

    // damage object was destroyed
    QCOMPARE(rgba_app->damageObject() == 0, true);
    QCOMPARE(rgba_app->pendingDamage(), false);

    // display on
    device_state->fake_display_off = false;
    cmgr->d->displayOff(false);
    QCOMPARE(cmgr->d->device_state->displayOff(), false);

    QCOMPARE(rgba_app->damageObject() != 0, true);
    fakeDamageEvent(cw);
    QCOMPARE(rgba_app->pendingDamage(), false);
}

// status menu open on top of opaque app window
void ut_Compositing::testDamageDuringTransparentMenu()
{
    fake_LMT_window *app = new fake_LMT_window(8);
    mapWindow(app);
    MCompositeWindow *app_cw = cmgr->d->windows.value(8, 0);
    fakeDamageEvent(app_cw);
    QCOMPARE(app->pendingDamage(), false);
    fakeDamageEvent(app_cw);
    QCOMPARE(app->pendingDamage(), false);
    while (app_cw->windowAnimator()->isActive())
        QTest::qWait(500); // wait the animation to finish
    QCOMPARE(app->damageObject() == 0, true);

    fake_LMT_window *menu = new fake_LMT_window(7);
    menu->setAlpha(true);
    menu->prependType(ATOM(_NET_WM_WINDOW_TYPE_MENU));
    mapWindow(menu);
    QTest::qWait(10); // run the idle handlers
    MCompositeWindow *menu_cw = cmgr->d->windows.value(7, 0);

    QCOMPARE(cmgr->d->compositing, true);
    QCOMPARE(cmgr->d->overlay_mapped, true);
    QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), false);

    QCOMPARE(menu->damageObject() != 0, true);
    QCOMPARE(app->damageObject() != 0, true);

    fakeDamageEvent(app_cw);
    QCOMPARE(app->pendingDamage(), false);

    fakeDamageEvent(menu_cw);
    QCOMPARE(menu->pendingDamage(), false);
}

// RGBA window that is first obscured and then gets revealed
// --- test that damage that was received while the window was obscured
// is handled as soon as it's visible again
void ut_Compositing::testDamageToObscuredRGBAWindow()
{
    fake_LMT_window *rgba_app = new fake_LMT_window(9);
    rgba_app->setAlpha(true);
    mapWindow(rgba_app);
    QTest::qWait(10); // run the idle handlers
    MCompositeWindow *rgba_cw = cmgr->d->windows.value(9, 0);
    fakeDamageEvent(rgba_cw);
    QCOMPARE(rgba_app->pendingDamage(), false);
    fakeDamageEvent(rgba_cw);
    QCOMPARE(rgba_app->pendingDamage(), false);
    while (rgba_cw->windowAnimator()->isActive())
        QTest::qWait(500); // wait the animation to finish
    QCOMPARE(rgba_app->damageObject() != 0, true);

    // obscure it with opaque window
    fake_LMT_window *rgb_app = new fake_LMT_window(10);
    mapWindow(rgb_app);
    MCompositeWindow *rgb_cw = cmgr->d->windows.value(10, 0);
    fakeDamageEvent(rgb_cw);
    QCOMPARE(rgb_app->pendingDamage(), false);
    fakeDamageEvent(rgb_cw);
    QCOMPARE(rgb_app->pendingDamage(), false);
    while (rgb_cw->windowAnimator()->isActive())
        QTest::qWait(500); // wait the animation to finish
    QCOMPARE(rgb_app->damageObject() == 0, true);
    QCOMPARE(rgba_app->damageObject() != 0, true);

    QCOMPARE(rgba_cw->isVisible(), false);
    QCOMPARE(rgba_cw->windowObscured(), true);

    // now damage the RGBA window (not handled)
    fakeDamageEvent(rgba_cw);
    QCOMPARE(rgba_app->pendingDamage(), true);
    fakeDamageEvent(rgba_cw);
    QCOMPARE(rgba_app->pendingDamage(), true);

    // unmap the opaque window to reveal the RGBA window
    unmapWindow(rgb_app);
    QTest::qWait(10); // run the idle handlers
    QCOMPARE(rgba_cw->isVisible(), true);
    QCOMPARE(rgba_cw->windowObscured(), false);

    // now damage the RGBA window (handled -- also the old damage)
    QCOMPARE(rgba_app->pendingDamage(), false);
    fakeDamageEvent(rgba_cw);
    QCOMPARE(rgba_app->pendingDamage(), false);
    fakeDamageEvent(rgba_cw);
    QCOMPARE(rgba_app->pendingDamage(), false);
}

// Smaller-than-fullscreen window that is first obscured and then gets revealed
// --- test that damage that was received while the window was obscured
// is handled as soon as it's visible again.
// NOTE: this is only necessary because smaller-than-fullscreen windows cause
// compositing in the current code.
void ut_Compositing::testDamageToObscuredSmallWindow()
{
    fake_LMT_window *dlg = new fake_LMT_window(11, dwidth / 2, dheight / 2);
    // use a dialog because those are not resized
    dlg->prependType(ATOM(_NET_WM_WINDOW_TYPE_DIALOG));
    mapWindow(dlg);
    QVERIFY(dlg->realGeometry().width() == dwidth / 2
            && dlg->realGeometry().height() == dheight / 2);
    QTest::qWait(10); // run the idle handlers
    MCompositeWindow *dlg_cw = cmgr->d->windows.value(11, 0);
    fakeDamageEvent(dlg_cw);
    QCOMPARE(dlg->pendingDamage(), false);
    fakeDamageEvent(dlg_cw);
    QCOMPARE(dlg->pendingDamage(), false);
    QVERIFY(!dlg_cw->windowAnimator()->isActive()); // no animation
    QCOMPARE(dlg->damageObject() != 0, true);

    QVERIFY(cmgr->d->compositing);

    // obscure it with opaque window
    fake_LMT_window *rgb_app = new fake_LMT_window(12);
    mapWindow(rgb_app);
    MCompositeWindow *rgb_cw = cmgr->d->windows.value(12, 0);
    fakeDamageEvent(rgb_cw);
    QCOMPARE(rgb_app->pendingDamage(), false);
    fakeDamageEvent(rgb_cw);
    QCOMPARE(rgb_app->pendingDamage(), false);
    while (rgb_cw->windowAnimator()->isActive())
        QTest::qWait(500); // wait the animation to finish
    QCOMPARE(rgb_app->damageObject() == 0, true);
    QCOMPARE(dlg->damageObject() != 0, true);

    QCOMPARE(dlg_cw->isVisible(), false);
    QCOMPARE(dlg_cw->windowObscured(), true);

    // now damage the dialog (not handled)
    fakeDamageEvent(dlg_cw);
    QCOMPARE(dlg->pendingDamage(), true);
    fakeDamageEvent(dlg_cw);
    QCOMPARE(dlg->pendingDamage(), true);

    // unmap the opaque window to reveal the dialog
    unmapWindow(rgb_app);
    QTest::qWait(10); // run the idle handlers
    QCOMPARE(dlg_cw->isVisible(), true);
    QCOMPARE(dlg_cw->windowObscured(), false);

    // now damage the dialog (handled -- also the old damage)
    QCOMPARE(dlg->pendingDamage(), false);
    fakeDamageEvent(dlg_cw);
    QCOMPARE(dlg->pendingDamage(), false);
    fakeDamageEvent(dlg_cw);
    QCOMPARE(dlg->pendingDamage(), false);
}

int main(int argc, char* argv[])
{
    // init fake but basic compositor environment
    QApplication::setGraphicsSystem("native");
    QCoreApplication::setLibraryPaths(QStringList());
    MCompositeManager app(argc, argv);
    
    XSetErrorHandler(error_handler);
    
    QGraphicsScene *scene = app.scene();
    QGraphicsView view(scene);
    view.setFrameStyle(0);
    
    QGLFormat fmt;
    fmt.setSamples(0);
    fmt.setSampleBuffers(false);

    QGLWidget w(fmt);
    w.setAttribute(Qt::WA_PaintOutsidePaintEvent);    
    w.setAutoFillBackground(false);
    dheight = QApplication::desktop()->height();
    dwidth = QApplication::desktop()->width();
    w.setMinimumSize(dwidth, dheight);
    w.setMaximumSize(dwidth, dheight);
    app.setGLWidget(&w);

    view.setViewport(&w);
    w.makeCurrent();

    ut_Compositing test;

    return QTest::qExec(&test, argc, argv);
}
