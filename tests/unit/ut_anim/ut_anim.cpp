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
#include "ut_anim.h"

#include <QtDebug>

#include <X11/Xlib.h>

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

    xcb_get_window_attributes_reply_t attrs;
    friend class ut_Anim;
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
    bool pendingDamage() { return pending_damage; }
    Damage damageObject() { return damage_object; }

    xcb_get_window_attributes_reply_t attrs;
};

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

    // create a fake desktop window
    fake_desktop_window *pc = new fake_desktop_window(1000);
    addWindow(pc);
    mapWindow(pc);
    MCompositeWindow *cw = cmgr->d->windows.value(1000, 0);
    QCOMPARE(cw != 0, true);
    QCOMPARE(cw->isValid(), true);
    QCOMPARE(cw->propertyCache()->windowType(), MCompAtoms::DESKTOP);
    QCOMPARE(cmgr->d->desktop_window == cw->window(), true);
}

// check that window that does not paint itself will be made visible
// after the timeout
void ut_Anim::testDamageTimeout()
{
    fake_LMT_window *pc = new fake_LMT_window(123, false);
    addWindow(pc);
    // create a fake MapNotify event
    mapWindow(pc);
    MCompositeWindow *cw = cmgr->d->windows.value(123, 0);
    QCOMPARE(cw != 0, true);
    QCOMPARE(cw->isVisible(), false);
    QTest::qWait(1000); // wait for the timeout
    QCOMPARE(cw->isVisible(), true);
    QCOMPARE(MCompositeWindow::we_have_grab, false);
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
    fake_LMT_window *pc = new fake_LMT_window(1, false);
    addWindow(pc);
    // create a fake MapNotify event
    mapWindow(pc);

    MCompositeWindow *cw = cmgr->d->windows.value(1, 0);
    QCOMPARE(cw != 0, true);
    QCOMPARE(cw->isValid(), true);
    QCOMPARE(cw->windowAnimator() != 0, true);

    fakeDamageEvent(cw);
    QCOMPARE(pc->pendingDamage(), false);
    fakeDamageEvent(cw);
    QCOMPARE(pc->pendingDamage(), false);

    QCOMPARE(MCompositeWindow::we_have_grab, true);
    QCOMPARE(cw->windowAnimator()->isActive(), true);

    while (cw->windowAnimator()->isActive())
        QTest::qWait(1000); // wait the animation to finish

    QCOMPARE(cw->propertyCache()->windowState(), NormalState);
    int d_i = cmgr->d->stacking_list.indexOf(1000);
    int w_i = cmgr->d->stacking_list.indexOf(1);
    QCOMPARE(d_i >= 0 && w_i >= 0, true);
    QCOMPARE(d_i < w_i, true);
    QCOMPARE(MCompositeWindow::we_have_grab, false);
}

void ut_Anim::testOpenChainingAnimation()
{
    fake_LMT_window *pc1 = (fake_LMT_window*)cmgr->d->prop_caches.value(1, 0);
    QCOMPARE(pc1 != 0, true);
    // need to set portrait too for NB#279547 workaround
    pc1->setOrientationAngle(270);

    fake_LMT_window *pc2 = new fake_LMT_window(2000, false);
    pc2->setInvokedBy(1);
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
    QCOMPARE(qobject_cast<MChainedAnimation*> (cw2->windowAnimator()) != 0, 
             true);
    
    fakeDamageEvent(cw2);
    QCOMPARE(pc2->pendingDamage(), false);
    fakeDamageEvent(cw2);
    QCOMPARE(pc2->pendingDamage(), false);

    // window position check
    QCOMPARE(cw2->windowAnimator()->isActive(), true);
    QRectF screen = QApplication::desktop()->availableGeometry();
    QCOMPARE(cw2->pos() == screen.translated(0,-screen.height()).topLeft(),
             true);
    QCOMPARE(MCompositeWindow::we_have_grab, true);

    while (cw2->windowAnimator()->isActive())
        QTest::qWait(1000); // wait the animation to finish
        
    MCompositeWindow *cw1 = cmgr->d->windows.value(1, 0);
    // window position check
    QCOMPARE(cw2->pos() == QPointF(0,0), true);
    QCOMPARE(cw1->pos() == screen.bottomLeft(), true);
    QCOMPARE(MCompositeWindow::we_have_grab, false);
}

void ut_Anim::testCloseChainingAnimation()
{
    MCompositeWindow *cw1 = cmgr->d->windows.value(1, 0);
    MCompositeWindow *cw2 = cmgr->d->windows.value(2000, false);

    XUnmapEvent ue;
    memset(&ue, 0, sizeof(ue));
    ue.window = 2000;
    ue.event = QX11Info::appRootWindow();
    ((MTexturePixmapItem*)cw2)->d->TFP.drawable = request_testpixmap();
    cmgr->d->unmapEvent(&ue);
    
    // should use chained
    QCOMPARE(qobject_cast<MChainedAnimation*> (cw2->windowAnimator()) != 0, 
             true);
    
    // window position check
    QCOMPARE(cw2->windowAnimator()->isActive(), true);
    QRectF screen = QApplication::desktop()->availableGeometry();
    QCOMPARE(cw2->pos() == QPointF(0,0), true);
    QCOMPARE(cw1->pos() == screen.bottomLeft(), true);
    QCOMPARE(MCompositeWindow::we_have_grab, true);
    
    while (cw2->windowAnimator()->isActive())
        QTest::qWait(1000); // wait the animation to finish
    // window position check
    QCOMPARE(cw2->pos() == screen.translated(0,-screen.height()).topLeft(), 
             true);
    QCOMPARE(cw1->pos() == QPointF(0,0), true);
    QCOMPARE(MCompositeWindow::we_have_grab, false);
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
    QCOMPARE(cw != 0, true);
    QCOMPARE(cw->isValid(), true);
    QCOMPARE(cw->windowAnimator() != 0, true);
    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QCOMPARE(MCompositeWindow::we_have_grab, true);
    // damage to desktop window is now handled
    QCOMPARE(d_pc->pendingDamage(), false);
    while (cw->windowAnimator()->isActive())
        QTest::qWait(1000); // wait the animation to finish

    QCOMPARE(cw->propertyCache()->windowState(), IconicState);
    int d_i = cmgr->d->stacking_list.indexOf(1000);
    int w_i = cmgr->d->stacking_list.indexOf(1);
    QCOMPARE(d_i >= 0 && w_i >= 0, true);
    QCOMPARE(d_i > w_i, true);
    QCOMPARE(MCompositeWindow::we_have_grab, false);

    // check that damage is not subtracted for iconic window
    fakeDamageEvent(cw);
    QCOMPARE(((fake_LMT_window*)cw->propertyCache())->pendingDamage(), true);

    // desktop is on top and should not have damage object
    QCOMPARE(d_pc->damageObject() == 0, true);
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
    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QCOMPARE(MCompositeWindow::we_have_grab, true);
    QCOMPARE(((fake_LMT_window*)cw->propertyCache())->pendingDamage(), false);
    while (cw->windowAnimator()->isActive())
        QTest::qWait(1000); // wait the animation to finish

    QCOMPARE(cw->propertyCache()->windowState(), NormalState);
    int d_i = cmgr->d->stacking_list.indexOf(1000);
    int w_i = cmgr->d->stacking_list.indexOf(1);
    QCOMPARE(d_i >= 0 && w_i >= 0, true);
    QCOMPARE(d_i < w_i, true);
    QCOMPARE(MCompositeWindow::we_have_grab, false);
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
    
    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QCOMPARE(MCompositeWindow::we_have_grab, true);
    // damage to desktop window is now handled
    QCOMPARE(d_pc->pendingDamage(), false);
    while (cw->windowAnimator()->isActive())
        QTest::qWait(1000); // wait the animation to finish
    QCOMPARE(cw->propertyCache()->windowState(), WithdrawnState);
    QCOMPARE(MCompositeWindow::we_have_grab, false);
}

void ut_Anim::testStartupAnimForSecondTimeMapped()
{
    fake_LMT_window *pc = new fake_LMT_window(2, false);
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
    QCOMPARE(MCompositeWindow::we_have_grab, true);

    while (cw->windowAnimator()->isActive())
        QTest::qWait(1000); // wait the animation to finish
    QCOMPARE(MCompositeWindow::we_have_grab, false);

    // map it again and check that there is an animation
    mapWindow(pc);

    fakeDamageEvent(cw);
    QCOMPARE(pc->pendingDamage(), false);
    fakeDamageEvent(cw);
    QCOMPARE(pc->pendingDamage(), false);

    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QCOMPARE(MCompositeWindow::we_have_grab, true);

    while (cw->windowAnimator()->isActive())
        QTest::qWait(1000); // wait the animation to finish

    QCOMPARE(cw->propertyCache()->windowState(), NormalState);
    int d_i = cmgr->d->stacking_list.indexOf(1000);
    int w_i = cmgr->d->stacking_list.indexOf(1);
    QCOMPARE(d_i >= 0 && w_i >= 0, true);
    QCOMPARE(d_i < w_i, true);
    QCOMPARE(MCompositeWindow::we_have_grab, false);

    MCompositeWindow *d = cmgr->d->windows.value(cmgr->d->desktop_window, 0);
    fake_desktop_window *d_pc = (fake_desktop_window*)d->propertyCache();
    QCOMPARE(d_pc->damageObject() != 0, true);
    // check that damage to desktop window is handled
    fakeDamageEvent(d);
    QCOMPARE(d_pc->pendingDamage(), false);
}

void ut_Anim::testNoAnimations()
{
    fake_LMT_window *pc = new fake_LMT_window(3, false);
    pc->no_animations = true;
    addWindow(pc);
    // create a fake MapNotify event
    mapWindow(pc);

    MCompositeWindow *cw = cmgr->d->windows.value(3, 0);
    QCOMPARE(cw != 0, true);
    QCOMPARE(cw->isValid(), true);
    QCOMPARE(cw->windowAnimator() != 0, true);

    fakeDamageEvent(cw);
    fakeDamageEvent(cw);

    // check that there is no animation
    QCOMPARE(cw->windowAnimator()->isActive(), false);
    QCOMPARE(MCompositeWindow::we_have_grab, false);
    QCOMPARE(cw->isMapped(), true);
    QCOMPARE(cw->isVisible(), true);
    QCOMPARE(cw->propertyCache()->windowState(), NormalState);
    int d_i = cmgr->d->stacking_list.indexOf(1000);
    int w_i = cmgr->d->stacking_list.indexOf(3);
    QCOMPARE(d_i >= 0 && w_i >= 0, true);
    QCOMPARE(d_i < w_i, true);

    // check that iconifying does not have animation
    cmgr->d->exposeSwitcher();
    QCOMPARE(cw->windowAnimator()->isActive(), false);
    QCOMPARE(MCompositeWindow::we_have_grab, false);
    QCOMPARE(cw->propertyCache()->windowState(), IconicState);
    d_i = cmgr->d->stacking_list.indexOf(1000);
    w_i = cmgr->d->stacking_list.indexOf(3);
    QCOMPARE(d_i >= 0 && w_i >= 0, true);
    QCOMPARE(d_i > w_i, true);

    // check that restore does not have animation
    XClientMessageEvent cme;
    memset(&cme, 0, sizeof(cme));
    cme.window = 3;
    cme.type = ClientMessage;
    cme.message_type = ATOM(_NET_ACTIVE_WINDOW);
    cmgr->d->rootMessageEvent(&cme);
    QCOMPARE(cw->windowAnimator()->isActive(), false);
    QCOMPARE(MCompositeWindow::we_have_grab, false);

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
    QCOMPARE(MCompositeWindow::we_have_grab, false);
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
    QCOMPARE(MCompositeWindow::we_have_grab, true);
    QCOMPARE(an->triggered == MCompositeWindowAnimation::Showing, true);
    an->triggered = MCompositeWindowAnimation::NoAnimation;
    while (cw->windowAnimator()->isActive())
        QTest::qWait(500); 

    QCOMPARE(MCompositeWindow::we_have_grab, false);
    // iconify
    cmgr->d->exposeSwitcher();
    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QCOMPARE(MCompositeWindow::we_have_grab, true);
    QCOMPARE(an->triggered == MCompositeWindowAnimation::Iconify, true);
    an->triggered = MCompositeWindowAnimation::NoAnimation;
    while (cw->windowAnimator()->isActive())
        QTest::qWait(500); 

    QCOMPARE(MCompositeWindow::we_have_grab, false);

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
    QCOMPARE(MCompositeWindow::we_have_grab, true);
    QCOMPARE(((fake_LMT_window*)cw->propertyCache())->pendingDamage(), false);
    while (cw->windowAnimator()->isActive())
        QTest::qWait(500); 

    QCOMPARE(MCompositeWindow::we_have_grab, false);
    // close 
    XUnmapEvent ue;
    memset(&ue, 0, sizeof(ue));
    ue.window = 1;
    ue.event = QX11Info::appRootWindow();
    cmgr->d->unmapEvent(&ue);
    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QCOMPARE(an->triggered == MCompositeWindowAnimation::Closing, true); 
    QCOMPARE(MCompositeWindow::we_have_grab, true);
    while (cw->windowAnimator()->isActive())
        QTest::qWait(500); 
    QCOMPARE(MCompositeWindow::we_have_grab, false);
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

    QCOMPARE(MCompositeWindow::we_have_grab, true);
    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QCOMPARE(an->triggered == MCompositeWindowAnimation::Showing, true);
    an->triggered = MCompositeWindowAnimation::NoAnimation;
    while (cw->windowAnimator()->isActive())
        QTest::qWait(500); 

    QCOMPARE(MCompositeWindow::we_have_grab, false);
    // iconify
    cmgr->d->exposeSwitcher();
    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QCOMPARE(an->triggered == MCompositeWindowAnimation::Iconify, true);
    an->triggered = MCompositeWindowAnimation::NoAnimation;
    QCOMPARE(MCompositeWindow::we_have_grab, true);
    while (cw->windowAnimator()->isActive())
        QTest::qWait(500); 

    // simulate damage while the window is iconic
    fakeDamageEvent(cw);
    QCOMPARE(((fake_LMT_window*)cw->propertyCache())->pendingDamage(), true);

    QCOMPARE(MCompositeWindow::we_have_grab, false);
    // restore
    XClientMessageEvent cme;
    memset(&cme, 0, sizeof(cme));
    cme.window = 1;
    cme.type = ClientMessage;
    cme.message_type = ATOM(_NET_ACTIVE_WINDOW);
    cmgr->d->rootMessageEvent(&cme);
    QCOMPARE(an->triggered == MCompositeWindowAnimation::Restore, true);
    an->triggered = MCompositeWindowAnimation::NoAnimation;    
    QCOMPARE(MCompositeWindow::we_have_grab, true);
    QCOMPARE(((fake_LMT_window*)cw->propertyCache())->pendingDamage(), false);
    while (cw->windowAnimator()->isActive())
        QTest::qWait(500); 

    QCOMPARE(MCompositeWindow::we_have_grab, false);
    // close 
    XUnmapEvent ue;
    memset(&ue, 0, sizeof(ue));
    ue.window = 1;
    ue.event = QX11Info::appRootWindow();
    cmgr->d->unmapEvent(&ue);
    QCOMPARE(cw->windowAnimator()->isActive(), true);
    QCOMPARE(MCompositeWindow::we_have_grab, true);
    QCOMPARE(an->triggered == MCompositeWindowAnimation::Closing, true); 
    while (cw->windowAnimator()->isActive())
        QTest::qWait(500);

    QCOMPARE(MCompositeWindow::we_have_grab, false);
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

    ut_Anim anim;

    return QTest::qExec(&anim);
}

#include "ut_anim.moc"
