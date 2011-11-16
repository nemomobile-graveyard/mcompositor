#include <QtTest/QtTest>
#include <QtGui>
#include <QGLWidget>
#include <mcompositemanager.h>
#include <mcompositemanager_p.h>
#include <mwindowpropertycache.h>
#include <mcompositewindow.h>
#include <mcompositewindowanimation.h>
#include <mtexturepixmapitem.h>
#include <mcompositescene.h>
#include <mdevicestate.h>
#include "ut_lockscreen.h"

#include <QtDebug>

#include <X11/Xlib.h>

static int dheight, dwidth;

// Skip bad window messages for mock windows
static int error_handler(Display * , XErrorEvent *)
{    
    return 0;
}

class fake_LMT_window : public MWindowPropertyCache
{
public:
    fake_LMT_window(Window w, bool is_mapped = true)
        : MWindowPropertyCache(w, &attrs)
    {
        cancelAllRequests();
        memset(&attrs, 0, sizeof(attrs));
        setIsMapped(is_mapped);
        setRealGeometry(QRect(0, 0, dwidth, dheight));
        // icon geometry can be required for iconifying animation
        icon_geometry = QRect(0, 0, dwidth / 2, dheight / 2);
        type_atoms.append(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
        window_state = NormalState;
        has_alpha = 0;
    }
    Damage damageObject() const { return damage_object; }
    bool pendingDamage() const { return pending_damage; }

    xcb_get_window_attributes_reply_t attrs;
    friend class ut_Lockscreen;
};

class fake_desktop_window : public MWindowPropertyCache
{
public:
    fake_desktop_window(Window w)
        : MWindowPropertyCache(w, &attrs)
    {
        cancelAllRequests();
        memset(&attrs, 0, sizeof(attrs));
        setIsMapped(true);
        setRealGeometry(QRect(0, 0, dwidth, dheight));
        type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_DESKTOP));
        window_state = NormalState;
        has_alpha = 0;
        is_valid = true;
    }

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
static fake_LMT_window *lockscreen;
static Window lockscreen_win = 1;

void ut_Lockscreen::mapWindow(MWindowPropertyCache *pc)
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

void ut_Lockscreen::unmapLockscreen()
{
    XUnmapEvent ue;
    memset(&ue, 0, sizeof(ue));
    ue.window = lockscreen_win;
    ue.event = QX11Info::appRootWindow();
    cmgr->d->unmapEvent(&ue);
}

void ut_Lockscreen::initTestCase()
{
    cmgr = (MCompositeManager*)qApp;
    cmgr->setSurfaceWindow(0);
    cmgr->d->prepare();
    cmgr->d->stacking_list.clear();
    cmgr->d->prop_caches.clear();
    cmgr->d->xserver_stacking.init();

    // create an altered MDeviceState
    device_state = new fake_device_state();
    delete cmgr->d->device_state;
    cmgr->d->device_state = device_state;
    device_state->fake_touchScreenLockMode = "locked";

    // create a fake desktop window
    fake_desktop_window *pc = new fake_desktop_window(1000);
    mapWindow(pc);
    MCompositeWindow *cw = cmgr->d->windows.value(1000, 0);
    QCOMPARE(cw != 0, true);
    QCOMPARE(cw->isValid(), true);

    // create an unmapped, fake lockscreen
    lockscreen = new fake_LMT_window(lockscreen_win, false);
    lockscreen->wm_name = "Screen Lock";
    lockscreen->meego_layer = 5;
    cmgr->d->prop_caches[lockscreen_win] = lockscreen;
    cmgr->d->xserver_stacking.windowCreated(lockscreen_win);

    QCOMPARE(lockscreen->isLockScreen(), true);

    mapWindow(lockscreen);
    QCOMPARE(lockscreen->damageObject() != 0, true);

    unmapLockscreen();
}

void ut_Lockscreen::testScreenOnBeforeLockscreenPaint()
{
    // display off
    device_state->fake_display_off = true;
    cmgr->d->displayOff(true);

    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(cmgr->d->compositing, true);

    // display on
    device_state->fake_display_off = false;
    cmgr->d->displayOff(false);

    // map lockscreen but don't paint it yet
    fake_LMT_window *pc = (fake_LMT_window*)cmgr->d->prop_caches.value(
                                                          lockscreen_win, 0);
    mapWindow(pc);

    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(pc->damageObject() != 0, true);

    MCompositeWindow *cw = cmgr->d->windows.value(lockscreen_win, 0);
    QCOMPARE(cw != 0, true);
    // paint it now
    fakeDamageEvent(cw);
    fakeDamageEvent(cw);

    QCOMPARE(cmgr->d->watch->keep_black, false);
    QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), true);
    QCOMPARE(pc->damageObject() == 0, true);

    unmapLockscreen();
}

void ut_Lockscreen::testScreenOnAfterLockscreenPaint()
{
    // display off
    device_state->fake_display_off = true;
    cmgr->d->displayOff(true);

    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(cmgr->d->compositing, true);

    // map and paint lockscreen
    fake_LMT_window *pc = (fake_LMT_window*)cmgr->d->prop_caches.value(
                                                          lockscreen_win, 0);
    mapWindow(pc);
    MCompositeWindow *cw = cmgr->d->windows.value(lockscreen_win, 0);
    QCOMPARE(cw != 0, true);
    QCOMPARE(pc->damageObject() != 0, true);
    fakeDamageEvent(cw);
    fakeDamageEvent(cw);

    QCOMPARE(cmgr->d->watch->keep_black, false);
    QCOMPARE(cmgr->d->compositing, false);

    // display on
    device_state->fake_display_off = false;
    cmgr->d->displayOff(false);

    QCOMPARE(cmgr->d->watch->keep_black, false);
    QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), true);
    QCOMPARE(pc->damageObject() == 0, true);

    unmapLockscreen();
}

void ut_Lockscreen::testScreenOnAfterMapButBeforePaint()
{
    // display off
    device_state->fake_display_off = true;
    cmgr->d->displayOff(true);

    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(cmgr->d->compositing, true);

    // map the lockscreen
    fake_LMT_window *pc = (fake_LMT_window*)cmgr->d->prop_caches.value(
                                                          lockscreen_win, 0);
    mapWindow(pc);
    MCompositeWindow *cw = cmgr->d->windows.value(lockscreen_win, 0);
    QCOMPARE(cw != 0, true);
    QCOMPARE(pc->damageObject() != 0, true);

    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(cmgr->d->compositing, true);

    // display on
    device_state->fake_display_off = false;
    cmgr->d->displayOff(false);

    // keeps black because lockscreen is not yet painted
    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(cmgr->d->compositing, true);

    // paint the lockscreen
    fakeDamageEvent(cw);
    fakeDamageEvent(cw);

    QCOMPARE(cmgr->d->watch->keep_black, false);
    QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), true);
    QCOMPARE(pc->damageObject() == 0, true);

    unmapLockscreen();
}

void ut_Lockscreen::testScreenOnThenMapsButDoesNotPaint()
{
    // display off
    device_state->fake_display_off = true;
    cmgr->d->displayOff(true);

    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(cmgr->d->compositing, true);

    // map the lockscreen
    fake_LMT_window *pc = (fake_LMT_window*)cmgr->d->prop_caches.value(
                                                          lockscreen_win, 0);
    mapWindow(pc);
    MCompositeWindow *cw = cmgr->d->windows.value(lockscreen_win, 0);
    QCOMPARE(cw != 0, true);
    QCOMPARE(pc->damageObject() != 0, true);

    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(cmgr->d->compositing, true);

    // display on
    device_state->fake_display_off = false;
    cmgr->d->displayOff(false);

    // keeps black because lockscreen is not yet painted
    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(cmgr->d->compositing, true);

    // wait for the painting timeout
    int t = qobject_cast<MCompositeManager*>(qApp)->
                         configInt("damage-timeout-ms");
    QTest::qWait(t + 100);

    QCOMPARE(cmgr->d->watch->keep_black, false);
    QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), true);
    QCOMPARE(pc->damageObject() == 0, true);

    unmapLockscreen();
}

void ut_Lockscreen::testScreenOnButLockscreenTimesOut()
{
    // display off
    device_state->fake_display_off = true;
    cmgr->d->displayOff(true);

    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(cmgr->d->compositing, true);

    // display on
    device_state->fake_display_off = false;
    cmgr->d->displayOff(false);

    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(cmgr->d->compositing, true);

    // wait for the timeout
    int t = qobject_cast<MCompositeManager*>(qApp)->
                         configInt("lockscreen-map-timeout-ms");
    QTest::qWait(t + 100);

    QCOMPARE(cmgr->d->watch->keep_black, false);
}

void ut_Lockscreen::testScreenOnAndThenQuicklyOff()
{
    // display off
    device_state->fake_display_off = true;
    cmgr->d->displayOff(true);

    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(cmgr->d->compositing, true);

    // display on
    device_state->fake_display_off = false;
    cmgr->d->displayOff(false);

    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(cmgr->d->compositing, true);

    // wait less than the timeout
    int t = qobject_cast<MCompositeManager*>(qApp)->
                         configInt("lockscreen-map-timeout-ms");
    QTest::qWait(t / 2);

    // display off
    device_state->fake_display_off = true;
    cmgr->d->displayOff(true);

    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(cmgr->d->compositing, true);

    // check that the timeout does not occur anymore
    QTest::qWait(t / 2 + 1);
    QCOMPARE(cmgr->d->watch->keep_black, true);
}

void ut_Lockscreen::testScreenOffAndThenQuicklyOn()
{
    // map the lockscreen
    fake_LMT_window *pc = (fake_LMT_window*)cmgr->d->prop_caches.value(
                                                          lockscreen_win, 0);
    mapWindow(pc);
    MCompositeWindow *cw = cmgr->d->windows.value(lockscreen_win, 0);
    QCOMPARE(cw != 0, true);
    QCOMPARE(pc->damageObject() != 0, true);

    // display off
    device_state->fake_display_off = true;
    cmgr->d->displayOff(true);

    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(cmgr->d->compositing, true);

    // display on
    device_state->fake_display_off = false;
    cmgr->d->displayOff(false);

    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(cmgr->d->compositing, true);

    // simulate case where lockscreen is still unmapping
    // because of screen off
    unmapLockscreen();

    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(cmgr->d->compositing, true);

    // map the lockscreen
    mapWindow(pc);

    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(cmgr->d->compositing, true);
    QCOMPARE(pc->damageObject() != 0, true);

    // paint the lockscreen
    fakeDamageEvent(cw);
    fakeDamageEvent(cw);

    QCOMPARE(cmgr->d->watch->keep_black, false);
    QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), true);
    QCOMPARE(pc->damageObject() == 0, true);
}

void ut_Lockscreen::fakeDamageEvent(MCompositeWindow *cw)
{
    XDamageNotifyEvent e;
    memset(&e, 0, sizeof(e));
    e.drawable = cw->window();
    cmgr->d->damageEvent(&e);
}

void ut_Lockscreen::testPaintingDuringScreenOff()
{
    unmapLockscreen();
    // map the lockscreen
    fake_LMT_window *pc = (fake_LMT_window*)cmgr->d->prop_caches.value(
                                                          lockscreen_win, 0);
    mapWindow(pc);
    MCompositeWindow *cw = cmgr->d->windows.value(lockscreen_win, 0);
    QCOMPARE(cw != 0, true);
    QCOMPARE(pc->damageObject() != 0, true);

    // display off
    device_state->fake_display_off = true;
    cmgr->d->displayOff(true);

    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(cmgr->d->compositing, true);

    // lockscreen damage is handled even when screen is off to avoid
    // a race condition
    fakeDamageEvent(cw);
    QCOMPARE(pc->pendingDamage(), false);

    // display on
    device_state->fake_display_off = false;
    cmgr->d->displayOff(false);

    QCOMPARE(cmgr->d->watch->keep_black, true);
    QCOMPARE(cmgr->d->compositing, true);
    QCOMPARE(cmgr->d->lockscreen_painted, false);
    QCOMPARE(cw->paintedAfterMapping(), false);

    QCOMPARE(pc->pendingDamage(), false);
    fakeDamageEvent(cw); // second damage stops the wait
    QCOMPARE(pc->pendingDamage(), false);
    QCOMPARE(cmgr->d->lockscreen_painted, true);
    QCOMPARE(cw->paintedAfterMapping(), true);
    QCOMPARE(cmgr->d->watch->keep_black, false);
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

    ut_Lockscreen test;

    return QTest::qExec(&test);
}
