#include <QtTest/QtTest>
#include <QtGui>
#include <QGLWidget>
#include <mcompositemanager.h>
#include <mcompositemanager_p.h>
#include <mwindowpropertycache.h>
#include <mcompositewindow.h>
#include <mcompositewindowanimation.h>
#include <mtexturepixmapitem.h>
#include <mdynamicanimation.h>
#include <mdevicestate.h>
#include "ut_anim.h"

#include <QtDebug>

#include <X11/Xlib.h>

#ifdef WINDOW_DEBUG
#define SET_PAINTED(X, V) ((MTexturePixmapItem*)X)->d->item_painted = V
#define VERIFY_PAINTED(X, V) \
               QVERIFY(((MTexturePixmapItem*)X)->d->item_painted >= V)
#define VERIFY_NOT_PAINTED(X, V) \
               QVERIFY(((MTexturePixmapItem*)X)->d->item_painted <= V)
#else
#define SET_PAINTED(X, V)
#define VERIFY_PAINTED(X, V)
#define VERIFY_NOT_PAINTED(X, V)
#endif

static int dheight, dwidth;

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

// initialized attrs for MWindowPropertyCache's constructor
static xcb_get_window_attributes_reply_t static_attrs;

class fake_LMT_window : public MWindowPropertyCache
{
public:
    fake_LMT_window(Window w)
        : MWindowPropertyCache(w, &static_attrs)
    {
        cancelAllRequests();
        memset(&fake_attrs, 0, sizeof(fake_attrs));
        attrs = &fake_attrs;
        setIsMapped(false);
        setRealGeometry(QRect(0, 0, dwidth, dheight));
        // icon geometry can be required for iconifying animation
        icon_geometry = QRect(0, 0, dwidth / 2, dheight / 2);
        type_atoms.append(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
        window_state = NormalState;
        has_alpha = 0;
        // mark valid to create animation object
        is_valid = true;
    }
    
    void setInvokedBy(Window w)
    {
        invoked_by = w;
    }
    void setOrientationAngle(unsigned a)
    {
        orientation_angle = a;
    }
    bool pendingDamage() { return pending_damage; }
    void appendType(Atom a) { type_atoms.append(a);
                              // set invalid to recalculate windowType()
                              window_type = MCompAtoms::INVALID; }
    void setTransientFor(Window w) { transient_for = w; }
    void addToTransients(Window w) { transients.append(w); }

    xcb_get_window_attributes_reply_t fake_attrs;
    friend class ut_Anim;
};

class fake_desktop_window : public MWindowPropertyCache
{
public:
    fake_desktop_window(Window w)
        : MWindowPropertyCache(w, &static_attrs)
    {
        cancelAllRequests();
        memset(&fake_attrs, 0, sizeof(fake_attrs));
        attrs = &fake_attrs;
        setIsMapped(true);
        setRealGeometry(QRect(0, 0, dwidth, dheight));
        type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_DESKTOP));
        window_state = NormalState;
        has_alpha = 0;
        is_valid = true;
    }
    bool pendingDamage() { return pending_damage; }
    Damage damageObject() { return damage_object; }

    xcb_get_window_attributes_reply_t fake_attrs;
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

void ut_Anim::addWindow(MWindowPropertyCache *pc)
{
    cmgr->d->prop_caches[pc->winId()] = pc;
    cmgr->d->xserver_stacking.windowCreated(pc->winId());
}

void ut_Anim::mapWindow(MWindowPropertyCache *pc)
{
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

void ut_Anim::initTestCase()
{
    cmgr = (MCompositeManager*)qApp;
    // initialize MCompositeManager
    cmgr->setSurfaceWindow(0);
    cmgr->d->prepare();
    cmgr->d->xserver_stacking.init();

    // effectively disable ungrab-grab delay for testing the grab logic
    cmgr->config("ungrab-grab-delay", 0);

    // create a fake desktop window
    fake_desktop_window *pc = new fake_desktop_window(1000);
    addWindow(pc);
    mapWindow(pc);
    MCompositeWindow *cw = cmgr->d->windows.value(1000, 0);
    QCOMPARE(cw != 0, true);
    QCOMPARE(cw->isValid(), true);
    QCOMPARE(cw->propertyCache()->windowType(), MCompAtoms::DESKTOP);
    QCOMPARE(cmgr->d->desktop_window == cw->window(), true);

    // simulate screen on and unlocked
    device_state = new fake_device_state();
    delete cmgr->d->device_state;
    cmgr->d->device_state = device_state;
    device_state->fake_touchScreenLockMode = "unlocked";
}

// check that window that does not paint itself will be made visible
// after the timeout
void ut_Anim::testDamageTimeout()
{
    fake_LMT_window *pc = new fake_LMT_window(123);
    addWindow(pc);
    // create a fake MapNotify event
    mapWindow(pc);
    MCompositeWindow *cw = cmgr->d->windows.value(123, 0);
    QCOMPARE(cw != 0, true);
    QCOMPARE(cw->isVisible(), false);
    QTest::qWait(1000); // wait for the timeout
    QCOMPARE(cw->isVisible(), true);
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
}

void ut_Anim::fakeDamageEvent(MCompositeWindow *cw)
{
    XDamageNotifyEvent e;
    memset(&e, 0, sizeof(e));
    e.drawable = cw->window();
    cmgr->d->damageEvent(&e);
}

void ut_Anim::testStartupAnimForFirstTimeMapped()
{
    fake_LMT_window *pc = new fake_LMT_window(1);
    addWindow(pc);
    // create a fake MapNotify event
    mapWindow(pc);

    MCompositeWindow *cw = cmgr->d->windows.value(1, 0);
    QCOMPARE(cw != 0, true);
    QCOMPARE(cw->isValid(), true);
    QCOMPARE(cw->windowAnimator() != 0, true);

    SET_PAINTED(cw, 0);
    fakeDamageEvent(cw);
    QCOMPARE(pc->pendingDamage(), false);
    fakeDamageEvent(cw);
    QCOMPARE(pc->pendingDamage(), false);

    QCOMPARE(cmgr->servergrab.hasGrab(), true);
    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QVERIFY(cmgr->d->compositing);

    while (cw->windowAnimator()->isActive())
        QTest::qWait(100); // wait the animation to finish

    VERIFY_PAINTED(cw, 10);
    QCOMPARE(cw->propertyCache()->windowState(), NormalState);
    int d_i = cmgr->d->stacking_list.indexOf(1000);
    int w_i = cmgr->d->stacking_list.indexOf(1);
    QCOMPARE(d_i >= 0 && w_i >= 0, true);
    QCOMPARE(d_i < w_i, true);
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
}

#define INVOKER 1111

void ut_Anim::testOpenChainingAnimation()
{
    fake_LMT_window *pc1 = new fake_LMT_window(INVOKER);
    QVERIFY(cmgr->ut_addWindow(pc1));
    MCompositeWindow *cw = cmgr->d->windows.value(INVOKER, 0);
    fakeDamageEvent(cw);
    fakeDamageEvent(cw);
    QCOMPARE(cw->windowAnimator()->isActive(), true);
    while (cw->windowAnimator()->isActive())
        QTest::qWait(100); // wait the animation to finish

    // need to set portrait too for NB#279547 workaround
    pc1->setOrientationAngle(270);

    fake_LMT_window *pc2 = new fake_LMT_window(2000);
    pc2->setInvokedBy(INVOKER);
    // portrait
    pc2->setOrientationAngle(270);

    addWindow(pc2);
    // invoked window
    mapWindow(pc2);

    MCompositeWindow *cw2 = cmgr->d->windows.value(2000, 0);
    QCOMPARE(cw2 != 0, true);
    QCOMPARE(cw2->isValid(), true);
    QCOMPARE(cw2->windowAnimator() != 0, true);
    
    // invoked should use chained
    QVERIFY(qobject_cast<MChainedAnimation*> (cw2->windowAnimator()));
    
    fakeDamageEvent(cw2);
    QCOMPARE(pc2->pendingDamage(), false);
    fakeDamageEvent(cw2);
    QCOMPARE(pc2->pendingDamage(), false);

    // window position check
    QCOMPARE(cw2->windowAnimator()->isActive(), true);
    QRectF screen = QApplication::desktop()->availableGeometry();
    QCOMPARE(cw2->pos() == screen.translated(0,-screen.height()).topLeft(),
             true);
    QCOMPARE(cmgr->servergrab.hasGrab(), true);
    QVERIFY(cmgr->d->compositing);

    while (cw2->windowAnimator()->isActive())
        QTest::qWait(100); // wait the animation to finish
    QVERIFY(!MCompositeWindow::hasTransitioningWindow());
        
    // window position check
    QCOMPARE(cw2->pos() == QPointF(0,0), true);
    QCOMPARE(cw->pos() == screen.bottomLeft(), true);
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
}

void ut_Anim::testCloseChainingAnimation()
{
    MCompositeWindow *cw1 = cmgr->d->windows.value(INVOKER, 0);
    MCompositeWindow *cw2 = cmgr->d->windows.value(2000, false);

    XUnmapEvent ue;
    memset(&ue, 0, sizeof(ue));
    ue.window = 2000;
    ue.event = QX11Info::appRootWindow();
    ((MTexturePixmapItem*)cw2)->d->TFP.drawable = request_testpixmap();
    cmgr->d->unmapEvent(&ue);
    
    // should use chained
    QVERIFY(qobject_cast<MChainedAnimation*> (cw2->windowAnimator()));
    
    // window position check
    QCOMPARE(cw2->windowAnimator()->isActive(), true);
    QRectF screen = QApplication::desktop()->availableGeometry();
    QCOMPARE(cw2->pos() == QPointF(0,0), true);
    QCOMPARE(cw1->pos() == screen.bottomLeft(), true);
    QCOMPARE(cmgr->servergrab.hasGrab(), true);
    QVERIFY(cmgr->d->compositing);
    
    while (cw2->windowAnimator()->isActive())
        QTest::qWait(100); // wait the animation to finish
    QVERIFY(!MCompositeWindow::hasTransitioningWindow());
    // window position check
    QCOMPARE(cw2->pos() == screen.translated(0,-screen.height()).topLeft(), 
             true);
    QCOMPARE(cw1->pos() == QPointF(0,0), true);
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
}

void ut_Anim::testOpenSheetAnimation()
{
    fake_LMT_window *pc1 = (fake_LMT_window*)
                            cmgr->d->prop_caches.value(INVOKER, 0);
    QVERIFY(pc1);
    QVERIFY(pc1->isMapped());
    QVERIFY(pc1->windowState() == NormalState);
    // portrait
    pc1->setOrientationAngle(270);

    // sheet
    fake_LMT_window *pc2 = new fake_LMT_window(3000);
    pc2->appendType(ATOM(_MEEGOTOUCH_NET_WM_WINDOW_TYPE_SHEET));
    pc2->setTransientFor(INVOKER);
    pc2->setInvokedBy(INVOKER); // why is transiency AND this needed?
    pc2->setOrientationAngle(270);
    pc1->addToTransients(3000);
    QVERIFY(cmgr->ut_addWindow(pc2));

    MCompositeWindow *cw2 = cmgr->d->windows.value(3000, 0);
    QVERIFY(cw2);
    QVERIFY(cw2->isValid());
    QVERIFY(cw2->windowAnimator());
    QVERIFY(qobject_cast<MSheetAnimation*>(cw2->windowAnimator()));

    fakeDamageEvent(cw2);
    QVERIFY(!pc2->pendingDamage());
    fakeDamageEvent(cw2);
    QVERIFY(!pc2->pendingDamage());

    // window position check
    QRectF screen = QApplication::desktop()->availableGeometry();
    QVERIFY(cw2->pos() == screen.topRight());
    MCompositeWindow *cw1 = cmgr->d->windows.value(INVOKER, 0);
    QVERIFY(cw1->pos() == QPointF(0, 0));

    QVERIFY(cw2->windowAnimator()->isActive());
    QVERIFY(cmgr->servergrab.hasGrab());
    QVERIFY(cmgr->d->compositing);

    while (cw2->windowAnimator()->isActive())
        QTest::qWait(100); // wait the animation to finish
    QVERIFY(!MCompositeWindow::hasTransitioningWindow());

    // window position check
    QVERIFY(cw2->pos() == QPointF(0, 0));
    QVERIFY(cw1->pos() == QPointF(0, 0));
    QVERIFY(!cmgr->servergrab.hasGrab());
}

void ut_Anim::testCloseSheetAnimation()
{
    MCompositeWindow *cw2 = cmgr->d->windows.value(3000, 0);
    QVERIFY(cw2);
    QVERIFY(cw2->isValid());
    QVERIFY(cw2->windowAnimator());
    QVERIFY(cw2->propertyCache()->transientFor() == INVOKER);
    MSheetAnimation *sheetanim;
    QVERIFY(sheetanim = qobject_cast<MSheetAnimation*>(cw2->windowAnimator()));

    XUnmapEvent ue;
    memset(&ue, 0, sizeof(ue));
    ue.window = 3000;
    ue.event = QX11Info::appRootWindow();
    ((MTexturePixmapItem*)cw2)->d->TFP.drawable = request_testpixmap();
    cmgr->d->unmapEvent(&ue);

    const MCompositeWindow *behind = sheetanim->behindWindow();
    QVERIFY(cw2->behind() == behind);
    QVERIFY(behind->isWindowTransitioning());
    QVERIFY(cw2->pos() == QPointF(0, 0));
    MCompositeWindow *cw1 = cmgr->d->windows.value(INVOKER, 0);
    QVERIFY(cw1->pos() == QPointF(0, 0));

    QVERIFY(cw2->windowAnimator()->isActive());
    QVERIFY(cmgr->servergrab.hasGrab());
    QVERIFY(cmgr->d->compositing);

    while (cw2->windowAnimator()->isActive())
        QTest::qWait(100); // wait the animation to finish
    QVERIFY(!behind->isWindowTransitioning());
    QVERIFY(!MCompositeWindow::hasTransitioningWindow());
    QVERIFY(!cmgr->d->compositing);
    QRectF screen = QApplication::desktop()->availableGeometry();
    QVERIFY(cw2->pos() == screen.topRight());
    QVERIFY(cw1->pos() == QPointF(0, 0));
    QVERIFY(!cmgr->servergrab.hasGrab());
}

void ut_Anim::testIconifyingAnimation()
{
    MCompositeWindow *d = cmgr->d->windows.value(cmgr->d->desktop_window, 0);
    fake_desktop_window *d_pc = (fake_desktop_window*)d->propertyCache();
    QCOMPARE(d_pc->damageObject() != 0, true);
    // check that damage to desktop window is handled (plugin requirement)
    fakeDamageEvent(d);
    QCOMPARE(d_pc->pendingDamage(), false);

    // iconifies window 1
    cmgr->d->exposeSwitcher();
    MCompositeWindow *cw = cmgr->d->windows.value(1, 0);
    SET_PAINTED(cw, 0);
    QCOMPARE(cw != 0, true);
    QCOMPARE(cw->isValid(), true);
    QCOMPARE(cw->windowAnimator() != 0, true);
    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QVERIFY(cmgr->d->compositing);
    QCOMPARE(cmgr->servergrab.hasGrab(), true);
    // damage to desktop window is now handled
    QCOMPARE(d_pc->pendingDamage(), false);
    while (cw->windowAnimator()->isActive())
        QTest::qWait(100); // wait the animation to finish
    QVERIFY(!MCompositeWindow::hasTransitioningWindow());

    VERIFY_PAINTED(cw, 10);
    QCOMPARE(cw->propertyCache()->windowState(), IconicState);
    int d_i = cmgr->d->stacking_list.indexOf(1000);
    int w_i = cmgr->d->stacking_list.indexOf(1);
    QCOMPARE(d_i >= 0 && w_i >= 0, true);
    QCOMPARE(d_i > w_i, true);
    QCOMPARE(cmgr->servergrab.hasGrab(), false);

    // check that damage is not subtracted for iconic window
    fakeDamageEvent(cw);
    QCOMPARE(((fake_LMT_window*)cw->propertyCache())->pendingDamage(), true);

    // desktop is on top and should not have damage object
    QCOMPARE(d_pc->damageObject() == 0, true);
}

// check that iconifying animation is skipped during lockscreen
void ut_Anim::testIconifyingAnimationBelowLockscreen()
{
    QVERIFY(!cmgr->d->compositing);
    // create a fake lockscreen
    fake_LMT_window *lockscreen = new fake_LMT_window(5000);
    lockscreen->wm_name = "Screen Lock";
    lockscreen->meego_layer = 5;
    addWindow(lockscreen);
    QVERIFY(lockscreen->isLockScreen());
    mapWindow(lockscreen);
    MCompositeWindow *lock_cw = cmgr->d->windows.value(5000, 0);
    fakeDamageEvent(lock_cw);
    fakeDamageEvent(lock_cw);
    QTest::qWait(10);
    QVERIFY(!cmgr->d->compositing);

    // show and iconify an app and check that there is no animation
    fake_LMT_window *app = new fake_LMT_window(4999);
    addWindow(app);
    mapWindow(app);
    MCompositeWindow *app_cw = cmgr->d->windows.value(4999, 0);
    SET_PAINTED(app_cw, 0);
    QVERIFY(!app_cw->windowAnimator()->isActive());
    QTest::qWait(10);
    QVERIFY(!cmgr->d->compositing); // hidden window: not animated

    XClientMessageEvent cme;
    memset(&cme, 0, sizeof(cme));
    cme.window = 4999;
    cme.type = ClientMessage;
    cme.message_type = ATOM(WM_CHANGE_STATE);
    cme.data.l[0] = IconicState;
    cme.format = 32;
    cmgr->d->clientMessageEvent(&cme);
    QVERIFY(!app_cw->windowAnimator()->isActive());
    QVERIFY(!cmgr->d->compositing);
    QVERIFY(!MCompositeWindow::hasTransitioningWindow());

    // unmap both windows
    XUnmapEvent ue;
    memset(&ue, 0, sizeof(ue));
    ue.window = 4999;
    ue.event = QX11Info::appRootWindow();
    cmgr->d->unmapEvent(&ue);
    QVERIFY(!cmgr->d->compositing);

    ue.window = 5000;
    cmgr->d->unmapEvent(&ue);
    while (lock_cw->windowAnimator()->isActive())
        QTest::qWait(100); // wait the animation to finish
    QVERIFY(!MCompositeWindow::hasTransitioningWindow());
    QVERIFY(!cmgr->d->compositing);
    VERIFY_NOT_PAINTED(app_cw, 0);
}

void ut_Anim::testRestoreAnimation()
{    
    MCompositeWindow *cw = cmgr->d->windows.value(1, 0);
    fakeDamageEvent(cw);
    QCOMPARE(((fake_LMT_window*)cw->propertyCache())->pendingDamage(), true);
    XClientMessageEvent cme;
    memset(&cme, 0, sizeof(cme));
    cme.window = 1;
    cme.type = ClientMessage;
    cme.message_type = ATOM(_NET_ACTIVE_WINDOW);
    cmgr->d->rootMessageEvent(&cme);
    SET_PAINTED(cw, 0);
    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QCOMPARE(cmgr->servergrab.hasGrab(), true);
    QCOMPARE(((fake_LMT_window*)cw->propertyCache())->pendingDamage(), false);
    while (cw->windowAnimator()->isActive())
        QTest::qWait(100); // wait the animation to finish
    QVERIFY(!MCompositeWindow::hasTransitioningWindow());

    VERIFY_PAINTED(cw, 10);
    QCOMPARE(cw->propertyCache()->windowState(), NormalState);
    QVERIFY(cw->propertyCache()->isMapped());
    int d_i = cmgr->d->stacking_list.indexOf(1000);
    int w_i = cmgr->d->stacking_list.indexOf(1);
    QCOMPARE(d_i >= 0 && w_i >= 0, true);
    QCOMPARE(d_i < w_i, true);
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
}

void ut_Anim::testCloseAnimation()
{
    MCompositeWindow *d = cmgr->d->windows.value(cmgr->d->desktop_window, 0);
    fake_desktop_window *d_pc = (fake_desktop_window*)d->propertyCache();
    QCOMPARE(d_pc->damageObject() != 0, true);
    // check that damage to desktop window is handled
    fakeDamageEvent(d);
    QCOMPARE(d_pc->pendingDamage(), false);

    MCompositeWindow *cw = cmgr->d->windows.value(1, 0);
    XUnmapEvent ue;
    memset(&ue, 0, sizeof(ue));
    ue.window = 1;
    ue.event = QX11Info::appRootWindow();
    ((MTexturePixmapItem*)cw)->d->TFP.drawable = request_testpixmap();
    cmgr->d->unmapEvent(&ue);
    
    SET_PAINTED(cw, 0);
    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QCOMPARE(cmgr->servergrab.hasGrab(), true);
    // damage to desktop window is now handled
    QCOMPARE(d_pc->pendingDamage(), false);
    while (cw->windowAnimator()->isActive())
        QTest::qWait(100); // wait the animation to finish
    QVERIFY(!MCompositeWindow::hasTransitioningWindow());
    VERIFY_PAINTED(cw, 10);
    QCOMPARE(cw->propertyCache()->windowState(), WithdrawnState);
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
}

void ut_Anim::testStartupAnimForSecondTimeMapped()
{
    fake_LMT_window *pc = new fake_LMT_window(2);
    addWindow(pc);
    // create a fake MapNotify event
    mapWindow(pc);

    MCompositeWindow *cw = cmgr->d->windows.value(2, 0);
    QCOMPARE(cw != 0, true);
    QCOMPARE(cw->isValid(), true);

    fakeDamageEvent(cw);
    QCOMPARE(pc->pendingDamage(), false);
    fakeDamageEvent(cw);
    QCOMPARE(pc->pendingDamage(), false);

    // create a fake UnmapNotify event
    XUnmapEvent ue;
    memset(&ue, 0, sizeof(ue));
    ue.window = 2;
    ue.event = QX11Info::appRootWindow();
    cmgr->d->unmapEvent(&ue);
    QCOMPARE(cmgr->servergrab.hasGrab(), true);
    QVERIFY(cmgr->d->compositing);

    while (cw->windowAnimator()->isActive())
        QTest::qWait(100); // wait the animation to finish
    QVERIFY(!MCompositeWindow::hasTransitioningWindow());
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
    QVERIFY(!cmgr->d->compositing);

    // map it again and check that there is an animation
    mapWindow(pc);

    SET_PAINTED(cw, 0);
    fakeDamageEvent(cw);
    QCOMPARE(pc->pendingDamage(), false);
    fakeDamageEvent(cw);
    QCOMPARE(pc->pendingDamage(), false);

    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QCOMPARE(cmgr->servergrab.hasGrab(), true);
    QVERIFY(cmgr->d->compositing);

    while (cw->windowAnimator()->isActive())
        QTest::qWait(100); // wait the animation to finish
    QVERIFY(!MCompositeWindow::hasTransitioningWindow());

    VERIFY_PAINTED(cw, 10);
    QCOMPARE(cw->propertyCache()->windowState(), NormalState);
    int d_i = cmgr->d->stacking_list.indexOf(1000);
    int w_i = cmgr->d->stacking_list.indexOf(1);
    QCOMPARE(d_i >= 0 && w_i >= 0, true);
    QCOMPARE(d_i < w_i, true);
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
    QVERIFY(!cmgr->d->compositing);

    MCompositeWindow *d = cmgr->d->windows.value(cmgr->d->desktop_window, 0);
    fake_desktop_window *d_pc = (fake_desktop_window*)d->propertyCache();
    QCOMPARE(d_pc->damageObject() != 0, true);
    // check that damage to desktop window is handled
    fakeDamageEvent(d);
    QCOMPARE(d_pc->pendingDamage(), false);
}

void ut_Anim::testNoAnimations()
{
    fake_LMT_window *pc = new fake_LMT_window(3);
    pc->no_animations = true;
    addWindow(pc);
    // create a fake MapNotify event
    mapWindow(pc);

    MCompositeWindow *cw = cmgr->d->windows.value(3, 0);
    SET_PAINTED(cw, 0);
    QCOMPARE(cw != 0, true);
    QCOMPARE(cw->isValid(), true);
    QCOMPARE(cw->windowAnimator() != 0, true);

    fakeDamageEvent(cw);
    fakeDamageEvent(cw);

    // check that there is no animation
    QCOMPARE(cw->windowAnimator()->isActive(), false);
    QVERIFY(!MCompositeWindow::hasTransitioningWindow());
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
    QCOMPARE(cw->isMapped(), true);
    QCOMPARE(cw->isVisible(), false);
    QCOMPARE(cw->propertyCache()->windowState(), NormalState);
    int d_i = cmgr->d->stacking_list.indexOf(1000);
    int w_i = cmgr->d->stacking_list.indexOf(3);
    QCOMPARE(d_i >= 0 && w_i >= 0, true);
    QCOMPARE(d_i < w_i, true);
    QVERIFY(!cmgr->d->compositing);

    // check that iconifying does not have animation
    cmgr->d->exposeSwitcher();
    QCOMPARE(cw->windowAnimator()->isActive(), false);
    QVERIFY(!MCompositeWindow::hasTransitioningWindow());
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
    QCOMPARE(cw->propertyCache()->windowState(), IconicState);
    d_i = cmgr->d->stacking_list.indexOf(1000);
    w_i = cmgr->d->stacking_list.indexOf(3);
    QCOMPARE(d_i >= 0 && w_i >= 0, true);
    QCOMPARE(d_i > w_i, true);
    QVERIFY(!cmgr->d->compositing);

    // check that restore does not have animation
    XClientMessageEvent cme;
    memset(&cme, 0, sizeof(cme));
    cme.window = 3;
    cme.type = ClientMessage;
    cme.message_type = ATOM(_NET_ACTIVE_WINDOW);
    cmgr->d->rootMessageEvent(&cme);
    QCOMPARE(cw->windowAnimator()->isActive(), false);
    QVERIFY(!MCompositeWindow::hasTransitioningWindow());
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
    QVERIFY(!cmgr->d->compositing);

    // create a fake UnmapNotify event
    XUnmapEvent ue;
    memset(&ue, 0, sizeof(ue));
    ue.window = 3;
    ue.event = QX11Info::appRootWindow();
    cmgr->d->unmapEvent(&ue);
    QCOMPARE(cw->isVisible(), false);
    QCOMPARE(cw->isMapped(), false);

    // check that there is no animation
    QCOMPARE(cw->windowAnimator()->isActive(), false);
    QVERIFY(!MCompositeWindow::hasTransitioningWindow());
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
    QVERIFY(!cmgr->d->compositing);
    VERIFY_NOT_PAINTED(cw, 0);
}

void ut_Anim::testSkipAnimationsWhenMeegoLevelWindowIsMapped()
{
    // first map a Meego stacking layer 1 window
    QVERIFY(cmgr->d->prop_caches.value(4, 0) == 0);
    fake_LMT_window *meegowin = new fake_LMT_window(4);
    meegowin->meego_layer = 1;
    addWindow(meegowin);
    mapWindow(meegowin);

    MCompositeWindow *meegocw = cmgr->d->windows.value(4, 0);
    QVERIFY(meegocw != 0);
    QVERIFY(meegocw->isValid());

    fakeDamageEvent(meegocw);
    fakeDamageEvent(meegocw);
    QTest::qWait(10);
    QVERIFY(cmgr->d->compositing);

    QVERIFY(meegocw->windowAnimator()->isActive());
    while (meegocw->windowAnimator()->isActive())
        QTest::qWait(100);

    // check animations are skipped for a normal application
    QVERIFY(cmgr->d->prop_caches.value(5, 0) == 0);
    fake_LMT_window *pc = new fake_LMT_window(5);
    addWindow(pc);
    mapWindow(pc);
    MCompositeWindow *cw = cmgr->d->windows.value(5, 0);
    SET_PAINTED(cw, 0);
    QVERIFY(cw != 0);
    QVERIFY(cw->isValid());
    fakeDamageEvent(cw);
    fakeDamageEvent(cw);
    QVERIFY(!cw->windowAnimator()->isActive());
    QTest::qWait(10);
    QVERIFY(!cmgr->d->compositing);

    XClientMessageEvent cme;
    memset(&cme, 0, sizeof(cme));
    cme.window = 5;
    cme.type = ClientMessage;
    cme.message_type = ATOM(WM_CHANGE_STATE);
    cme.data.l[0] = IconicState;
    cme.format = 32;
    cmgr->d->clientMessageEvent(&cme);
    QVERIFY(!cw->windowAnimator()->isActive());
    QVERIFY(!cmgr->d->compositing);

    memset(&cme, 0, sizeof(cme));
    cme.window = 5;
    cme.type = ClientMessage;
    cme.message_type = ATOM(_NET_ACTIVE_WINDOW);
    cmgr->d->rootMessageEvent(&cme);
    QVERIFY(!cw->windowAnimator()->isActive());
    QVERIFY(!cmgr->d->compositing);

    // unmap both
    XUnmapEvent ue;
    memset(&ue, 0, sizeof(ue));
    ue.window = 5;
    ue.event = QX11Info::appRootWindow();
    ((MTexturePixmapItem*)cw)->d->TFP.drawable = request_testpixmap();
    cmgr->d->unmapEvent(&ue);
    QVERIFY(!cw->windowAnimator()->isActive());

    ue.window = 4;
    ((MTexturePixmapItem*)meegocw)->d->TFP.drawable = request_testpixmap();
    cmgr->d->unmapEvent(&ue);

    QVERIFY(meegocw->windowAnimator()->isActive());
    while (meegocw->windowAnimator()->isActive())
        QTest::qWait(100);
    QVERIFY(!cmgr->d->compositing);
    VERIFY_NOT_PAINTED(cw, 0);
}

void ut_Anim::testSkipAnimationsWhenSystemModalIsMapped()
{
    // first map a system-modal dialog
    QVERIFY(cmgr->d->prop_caches.value(6, 0) == 0);
    fake_LMT_window *dlg = new fake_LMT_window(6);
    dlg->type_atoms.prepend(ATOM(_NET_WM_WINDOW_TYPE_DIALOG));
    dlg->net_wm_state.prepend(ATOM(_NET_WM_STATE_MODAL));
    addWindow(dlg);
    mapWindow(dlg);

    MCompositeWindow *dlg_cw = cmgr->d->windows.value(6, 0);
    QVERIFY(dlg_cw != 0);
    QVERIFY(dlg_cw->isValid());

    fakeDamageEvent(dlg_cw);
    fakeDamageEvent(dlg_cw);
    QTest::qWait(10);
    QVERIFY(!cmgr->d->compositing);
    QVERIFY(!dlg_cw->windowAnimator()->isActive());

    // check animations are skipped for a normal application
    QVERIFY(cmgr->d->prop_caches.value(7, 0) == 0);
    fake_LMT_window *pc = new fake_LMT_window(7);
    addWindow(pc);
    mapWindow(pc);
    MCompositeWindow *cw = cmgr->d->windows.value(7, 0);
    SET_PAINTED(cw, 0);
    QVERIFY(cw != 0);
    QVERIFY(cw->isValid());
    fakeDamageEvent(cw);
    fakeDamageEvent(cw);
    QVERIFY(!cw->windowAnimator()->isActive());
    QTest::qWait(10);
    QVERIFY(!cmgr->d->compositing);

    XClientMessageEvent cme;
    memset(&cme, 0, sizeof(cme));
    cme.window = 7;
    cme.type = ClientMessage;
    cme.message_type = ATOM(WM_CHANGE_STATE);
    cme.data.l[0] = IconicState;
    cme.format = 32;
    cmgr->d->clientMessageEvent(&cme);
    QVERIFY(!cw->windowAnimator()->isActive());
    QVERIFY(!cmgr->d->compositing);

    memset(&cme, 0, sizeof(cme));
    cme.window = 7;
    cme.type = ClientMessage;
    cme.message_type = ATOM(_NET_ACTIVE_WINDOW);
    cmgr->d->rootMessageEvent(&cme);
    QVERIFY(!cw->windowAnimator()->isActive());
    QVERIFY(!cmgr->d->compositing);

    // unmap both
    XUnmapEvent ue;
    memset(&ue, 0, sizeof(ue));
    ue.window = 7;
    ue.event = QX11Info::appRootWindow();
    ((MTexturePixmapItem*)cw)->d->TFP.drawable = request_testpixmap();
    cmgr->d->unmapEvent(&ue);
    QVERIFY(!cw->windowAnimator()->isActive());

    ue.window = 6;
    ((MTexturePixmapItem*)dlg_cw)->d->TFP.drawable = request_testpixmap();
    cmgr->d->unmapEvent(&ue);
    QVERIFY(!dlg_cw->windowAnimator()->isActive());
    QVERIFY(!cmgr->d->compositing);
    VERIFY_NOT_PAINTED(cw, 0);
}

void ut_Anim::testDontSkipAnimationsWhenHigherMeegoLevelWindowIsMapped()
{
    // first map a Meego stacking layer 1 window
    QVERIFY(cmgr->d->prop_caches.value(8, 0) == 0);
    fake_LMT_window *meegowin = new fake_LMT_window(8);
    meegowin->meego_layer = 1;
    addWindow(meegowin);
    mapWindow(meegowin);

    MCompositeWindow *meegocw = cmgr->d->windows.value(8, 0);
    QVERIFY(meegocw != 0);
    QVERIFY(meegocw->isValid());

    fakeDamageEvent(meegocw);
    fakeDamageEvent(meegocw);
    QTest::qWait(10);
    QVERIFY(cmgr->d->compositing);

    QVERIFY(meegocw->windowAnimator()->isActive());
    while (meegocw->windowAnimator()->isActive())
        QTest::qWait(100);

    // check animations are not skipped for a Meego level 2 window
    QVERIFY(cmgr->d->prop_caches.value(9, 0) == 0);
    fake_LMT_window *pc = new fake_LMT_window(9);
    pc->meego_layer = 2;
    addWindow(pc);
    mapWindow(pc);
    MCompositeWindow *cw = cmgr->d->windows.value(9, 0);
    SET_PAINTED(cw, 0);
    QVERIFY(cw != 0);
    QVERIFY(cw->isValid());
    fakeDamageEvent(cw);
    fakeDamageEvent(cw);
    QVERIFY(cw->windowAnimator()->isActive());
    QVERIFY(cmgr->d->compositing);
    while (cw->windowAnimator()->isActive())
        QTest::qWait(100);

    VERIFY_PAINTED(cw, 10);
    XClientMessageEvent cme;
    memset(&cme, 0, sizeof(cme));
    cme.window = 9;
    cme.type = ClientMessage;
    cme.message_type = ATOM(WM_CHANGE_STATE);
    cme.data.l[0] = IconicState;
    cme.format = 32;
    cmgr->d->clientMessageEvent(&cme);
    SET_PAINTED(cw, 0);
    QVERIFY(cw->windowAnimator()->isActive());
    QVERIFY(cmgr->d->compositing);
    while (cw->windowAnimator()->isActive())
        QTest::qWait(100);

    VERIFY_PAINTED(cw, 10);
    memset(&cme, 0, sizeof(cme));
    cme.window = 9;
    cme.type = ClientMessage;
    cme.message_type = ATOM(_NET_ACTIVE_WINDOW);
    cmgr->d->rootMessageEvent(&cme);
    // pluginless config does not animate this because desktop is not exposed
    QVERIFY(!cw->windowAnimator()->isActive());
    QVERIFY(!cmgr->d->compositing);
    QTest::qWait(10);

    // unmap both
    XUnmapEvent ue;
    memset(&ue, 0, sizeof(ue));
    ue.window = 9;
    ue.event = QX11Info::appRootWindow();
    ((MTexturePixmapItem*)cw)->d->TFP.drawable = request_testpixmap();
    cmgr->d->unmapEvent(&ue);
    SET_PAINTED(cw, 0);
    QVERIFY(!pc->isMapped());
    QVERIFY(cw->windowAnimator()->isActive());
    while (cw->windowAnimator()->isActive())
        QTest::qWait(100);

    VERIFY_PAINTED(cw, 10);
    ue.window = 8;
    ((MTexturePixmapItem*)meegocw)->d->TFP.drawable = request_testpixmap();
    cmgr->d->unmapEvent(&ue);
    QVERIFY(!meegowin->isMapped());

    QVERIFY(meegocw->windowAnimator()->isActive());
    while (meegocw->windowAnimator()->isActive())
        QTest::qWait(100);
    QVERIFY(!cmgr->d->compositing);
}


class DerivedAnimationTest: public MCompositeWindowAnimation
{
public:
    DerivedAnimationTest(QObject* parent)
        :MCompositeWindowAnimation(parent),
         triggered(MCompositeWindowAnimation::NoAnimation)
    {}
    
    void windowShown()
    {
        triggered = MCompositeWindowAnimation::Showing;
        MCompositeWindowAnimation::windowShown();
    }
    
    void windowClosed()
    {
        triggered = MCompositeWindowAnimation::Closing;   
        MCompositeWindowAnimation::windowClosed();
    }
    
    void windowIconified()
    {
        triggered = MCompositeWindowAnimation::Iconify;
        MCompositeWindowAnimation::windowIconified();
    }
    
    void windowRestored()
    {
        triggered = MCompositeWindowAnimation::Restore;
        MCompositeWindowAnimation::windowRestored();
    }

    MCompositeWindowAnimation::AnimationType triggered;
};

class AnimHandlerTest: public QObject, public MAbstractAnimationHandler
{
    Q_OBJECT
public:    
    AnimHandlerTest(MCompositeWindow* p)
        :QObject(p),
         triggered(MCompositeWindowAnimation::NoAnimation),
         window(p)
    {
    }

public slots:
    void windowShown()
    {
        window->windowAnimator()->start();
        triggered = MCompositeWindowAnimation::Showing;
    }
    
    void windowClosed()
    {
        window->windowAnimator()->start();
        triggered = MCompositeWindowAnimation::Closing;  
    }

    void windowIconified()
    {
        window->windowAnimator()->start();
        triggered = MCompositeWindowAnimation::Iconify;
    }
    
    void windowRestored()
    {
        window->windowAnimator()->start();
        triggered = MCompositeWindowAnimation::Restore;
    }
public:
    MCompositeWindowAnimation::AnimationType triggered;
    MCompositeWindow* window;
};

void ut_Anim::testDerivedAnimHandler()
{
    DerivedAnimationTest* an = new DerivedAnimationTest(0);
    MCompositeWindow *cw = cmgr->d->windows.value(1, 0);
    an->setTargetWindow(cw);

    // window shown
    ((MTexturePixmapItem*)cw)->d->TFP.drawable = request_testpixmap();
    mapWindow(cw->propertyCache());
    fakeDamageEvent(cw);
    QCOMPARE(((fake_LMT_window*)cw->propertyCache())->pendingDamage(), false);
    fakeDamageEvent(cw);
    QCOMPARE(((fake_LMT_window*)cw->propertyCache())->pendingDamage(), false);

    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QCOMPARE(cmgr->servergrab.hasGrab(), true);
    QVERIFY(cmgr->d->compositing);
    QCOMPARE(an->triggered == MCompositeWindowAnimation::Showing, true);
    an->triggered = MCompositeWindowAnimation::NoAnimation;
    while (cw->windowAnimator()->isActive())
        QTest::qWait(100); 

    QCOMPARE(cmgr->servergrab.hasGrab(), false);
    // iconify
    cmgr->d->exposeSwitcher();
    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QCOMPARE(cmgr->servergrab.hasGrab(), true);
    QVERIFY(cmgr->d->compositing);
    QCOMPARE(an->triggered == MCompositeWindowAnimation::Iconify, true);
    an->triggered = MCompositeWindowAnimation::NoAnimation;
    while (cw->windowAnimator()->isActive())
        QTest::qWait(100); 

    QCOMPARE(cmgr->servergrab.hasGrab(), false);

    // simulate damage while the window is iconic
    fakeDamageEvent(cw);
    QCOMPARE(((fake_LMT_window*)cw->propertyCache())->pendingDamage(), true);

    // restore
    XClientMessageEvent cme;
    memset(&cme, 0, sizeof(cme));
    cme.window = 1;
    cme.type = ClientMessage;
    cme.message_type = ATOM(_NET_ACTIVE_WINDOW);
    cmgr->d->rootMessageEvent(&cme);
    QCOMPARE(an->triggered == MCompositeWindowAnimation::Restore, true);
    an->triggered = MCompositeWindowAnimation::NoAnimation;    
    QCOMPARE(cmgr->servergrab.hasGrab(), true);
    QCOMPARE(((fake_LMT_window*)cw->propertyCache())->pendingDamage(), false);
    while (cw->windowAnimator()->isActive())
        QTest::qWait(100); 

    QCOMPARE(cmgr->servergrab.hasGrab(), false);
    // close 
    XUnmapEvent ue;
    memset(&ue, 0, sizeof(ue));
    ue.window = 1;
    ue.event = QX11Info::appRootWindow();
    cmgr->d->unmapEvent(&ue);
    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QCOMPARE(an->triggered == MCompositeWindowAnimation::Closing, true); 
    QCOMPARE(cmgr->servergrab.hasGrab(), true);
    QVERIFY(cmgr->d->compositing);
    while (cw->windowAnimator()->isActive())
        QTest::qWait(100); 
    QCOMPARE(cmgr->servergrab.hasGrab(), false);
}

void ut_Anim::testExternalAnimHandler()
{
    MCompositeWindow *cw = cmgr->d->windows.value(1, 0);
    AnimHandlerTest* an = new AnimHandlerTest(cw);
    cw->windowAnimator()->setAnimationHandler(MCompositeWindowAnimation::Showing,
                                              an);
    cw->windowAnimator()->setAnimationHandler(MCompositeWindowAnimation::Closing,
                                              an);
    cw->windowAnimator()->setAnimationHandler(MCompositeWindowAnimation::Iconify,
                                              an);
    cw->windowAnimator()->setAnimationHandler(MCompositeWindowAnimation::Restore, 
                                              an);                                    
    // window shown
    ((MTexturePixmapItem*)cw)->d->TFP.drawable = request_testpixmap();
    mapWindow(cw->propertyCache());
    fakeDamageEvent(cw);
    QCOMPARE(((fake_LMT_window*)cw->propertyCache())->pendingDamage(), false);
    fakeDamageEvent(cw);
    QCOMPARE(((fake_LMT_window*)cw->propertyCache())->pendingDamage(), false);

    QCOMPARE(cmgr->servergrab.hasGrab(), true);
    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QCOMPARE(an->triggered == MCompositeWindowAnimation::Showing, true);
    an->triggered = MCompositeWindowAnimation::NoAnimation;
    QVERIFY(cmgr->d->compositing);
    while (cw->windowAnimator()->isActive())
        QTest::qWait(100); 

    QCOMPARE(cmgr->servergrab.hasGrab(), false);
    // iconify
    cmgr->d->exposeSwitcher();
    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QCOMPARE(an->triggered == MCompositeWindowAnimation::Iconify, true);
    an->triggered = MCompositeWindowAnimation::NoAnimation;
    QCOMPARE(cmgr->servergrab.hasGrab(), true);
    QVERIFY(cmgr->d->compositing);
    while (cw->windowAnimator()->isActive())
        QTest::qWait(100); 

    // simulate damage while the window is iconic
    fakeDamageEvent(cw);
    QCOMPARE(((fake_LMT_window*)cw->propertyCache())->pendingDamage(), true);

    QCOMPARE(cmgr->servergrab.hasGrab(), false);
    // restore
    XClientMessageEvent cme;
    memset(&cme, 0, sizeof(cme));
    cme.window = 1;
    cme.type = ClientMessage;
    cme.message_type = ATOM(_NET_ACTIVE_WINDOW);
    cmgr->d->rootMessageEvent(&cme);
    QCOMPARE(an->triggered == MCompositeWindowAnimation::Restore, true);
    an->triggered = MCompositeWindowAnimation::NoAnimation;    
    QCOMPARE(cmgr->servergrab.hasGrab(), true);
    QCOMPARE(((fake_LMT_window*)cw->propertyCache())->pendingDamage(), false);
    while (cw->windowAnimator()->isActive())
        QTest::qWait(100); 

    QCOMPARE(cmgr->servergrab.hasGrab(), false);
    // close 
    XUnmapEvent ue;
    memset(&ue, 0, sizeof(ue));
    ue.window = 1;
    ue.event = QX11Info::appRootWindow();
    cmgr->d->unmapEvent(&ue);
    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QCOMPARE(cmgr->servergrab.hasGrab(), true);
    QVERIFY(cmgr->d->compositing);
    QCOMPARE(an->triggered == MCompositeWindowAnimation::Closing, true); 
    while (cw->windowAnimator()->isActive())
        QTest::qWait(100);

    QCOMPARE(cmgr->servergrab.hasGrab(), false);
}

void ut_Anim::testGrabberRace()
{
    QVERIFY(!cmgr->servergrab.hasGrab());
    QVERIFY(!cmgr->servergrab.needs_grab);

    // make sure we delay grabbing
    cmgr->config("ungrab-grab-delay", 100000000);
    cmgr->servergrab.grabLater();
    cmgr->servergrab.timeSinceLastUngrab.start();
    cmgr->servergrab.delayedGrabTimer.start();
    cmgr->servergrab.commit();
    cmgr->servergrab.ungrab();
    // Now both timers must be stopped
    // If mercytimer is not stopped it will fire every 50ms.
    QVERIFY(!cmgr->servergrab.mercytimer.isActive());
    QVERIFY(!cmgr->servergrab.delayedGrabTimer.isActive());
    cmgr->config("ungrab-grab-delay", 0);
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
    view.setProperty("NoMStyle", true);
    view.setUpdatesEnabled(false);
    view.setAutoFillBackground(false);
    view.setBackgroundBrush(Qt::NoBrush);
    view.setForegroundBrush(Qt::NoBrush);
    view.setFrameShadow(QFrame::Plain);

    view.setWindowFlags(Qt::X11BypassWindowManagerHint);
    view.setAttribute(Qt::WA_NoSystemBackground);
    view.setViewportUpdateMode(QGraphicsView::NoViewportUpdate);
    view.setOptimizationFlags(QGraphicsView::IndirectPainting);
    app.setSurfaceWindow(view.winId());
    
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
    view.show();

    ut_Anim anim;

    return QTest::qExec(&anim, argc, argv);
}

#include "ut_anim.moc"
