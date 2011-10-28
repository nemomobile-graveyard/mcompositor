#include <QtTest/QtTest>
#include <QtGui>
#include <QGLWidget>
#include <QtDebug>

#include "ut_splashscreen.h"

#include <mcompositemanager.h>
#include <mcompositemanager_p.h>
#include <mwindowpropertycache.h>
#include <mcompositewindow.h>
#include <mcompositewindowanimation.h>
#include <mtexturepixmapitem.h>
#include <mdynamicanimation.h>
#include <msplashscreen.h>
#include <mdevicestate.h>

#include <signal.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

// white one-pixel jpeg
static const unsigned jpeg_data[] = {
0xe0ffd8ff, 0x464a1000, 0x01004649, 0x48000101, 0x00004800,
0x1300feff, 0x61657243, 0x20646574, 0x68746977, 0x4d494720,
0x00dbff50, 0x27390043, 0x242b322b, 0x322e3239, 0x44393d40,
0x565d8f56, 0xaf564f4f, 0x8f68847d, 0xd6dab6cf, 0xc4c8b6cb,
0xffffffe4, 0xf6fff3e4, 0xffffc8c4, 0xffffffff, 0xddffffff,
0xffffffff, 0xffffffff, 0xdbffffff, 0x3d014300, 0x4b564040,
0x5d5da856, 0xc8ecffa8, 0xffffffec, 0xffffffff, 0xffffffff,
0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
0x081100c0, 0x01000100, 0x00220103, 0x03011102, 0xc4ff0111,
0x01001500, 0x00000001, 0x00000000, 0x00000000, 0x00000000,
0x00c4ff05, 0x00011014, 0x00000000, 0x00000000, 0x00000000,
0xff000000, 0x011400c4, 0x00000001, 0x00000000, 0x00000000,
0x00000000, 0x00c4ff00, 0x00011114, 0x00000000, 0x00000000,
0x00000000, 0xff000000, 0x030c00da, 0x11020001, 0x3f001103,
0x3f00a400, 0x0000d9ff
};

#define P_SPLASH_FILE "/tmp/mc-test-splash-p.jpg"
#define L_SPLASH_FILE "/tmp/mc-test-splash-l.jpg"

static int dheight, dwidth;

// Skip bad window messages for mock windows
static int error_handler(Display * , XErrorEvent *)
{
    return 0;
}

// initialized attrs for MWindowPropertyCache's constructor
static xcb_get_window_attributes_reply_t static_attrs;

class fake_LMT_window : public MWindowPropertyCache
{
public:
    fake_LMT_window(Window w, bool is_mapped = true)
        : MWindowPropertyCache(w, &static_attrs)
    {
        cancelAllRequests();
        memset(&fake_attrs, 0, sizeof(fake_attrs));
        attrs = &fake_attrs;
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

    void setProcessId(unsigned processId)
    {
        wm_pid = processId;
    }

    void setNoAnimations(unsigned no_a)
    {
        no_animations = no_a;
    }

    xcb_get_window_attributes_reply_t fake_attrs;
    friend class ut_splashscreen;
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
        type_atoms.append(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
        window_state = NormalState;
        has_alpha = 0;
        is_valid = true;
    }
    void setOrientationAngle(unsigned a)
    {
        orientation_angle = a;
    }

    xcb_get_window_attributes_reply_t fake_attrs;
};

class fake_device_state : public MDeviceState
{
public:
    fake_device_state()
        : fake_display_off(false), fake_is_flat(false),
          fake_touchScreenLockMode("unlocked"), fake_topedge("top")
          {}

    bool displayOff() const { return fake_display_off; }
    bool isFlat() const { return fake_is_flat; }
    const QString &touchScreenLock() const { return fake_touchScreenLockMode; }
    const QString &screenTopEdge() const { return fake_topedge; }

    bool fake_display_off, fake_is_flat;
    QString fake_touchScreenLockMode, fake_topedge;
};

QList<QPair<pid_t, int> > kill_calls;
static fake_device_state *device_state;

extern "C" int kill(pid_t pid, int sig)
{
    kill_calls.append(QPair<pid_t, int>(pid, sig));
    return 0;
}

void ut_splashscreen::addWindow(MWindowPropertyCache *pc)
{
    cmgr->d->prop_caches[pc->winId()] = pc;
    cmgr->d->xserver_stacking.windowCreated(pc->winId());
    if (!cmgr->d->stacking_list.contains(pc->winId()))
        cmgr->d->stacking_list.append(pc->winId());
}

void ut_splashscreen::mapWindow(MWindowPropertyCache *pc)
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

static bool anim_finished;

void ut_splashscreen::onAnimationsFinished(MCompositeWindow *cw)
{
    Q_UNUSED(cw);
    anim_finished = true;
}

// make sure no animations are running and composition is turned off
void ut_splashscreen::verifyDisabledComposition(MCompositeWindow *cw)
{
    anim_finished = false;
    connect(cw, SIGNAL(lastAnimationFinished(MCompositeWindow *)),
            SLOT(onAnimationsFinished(MCompositeWindow *)));

    while (!anim_finished)
        QTest::qWait(500);
    disconnect(cw, SIGNAL(lastAnimationFinished(MCompositeWindow *)),
               this, SLOT(onAnimationsFinished(MCompositeWindow *)));

    QVERIFY(!cmgr->d->compositing);
}

void ut_splashscreen::initTestCase()
{
    // create two jpeg files
    QFile splashp(P_SPLASH_FILE);
    QVERIFY(splashp.open(QIODevice::WriteOnly));
    splashp.write((const char*)jpeg_data, sizeof(jpeg_data));
    splashp.close();
    QFile splashl(L_SPLASH_FILE);
    QVERIFY(splashl.open(QIODevice::WriteOnly));
    splashl.write((const char*)jpeg_data, sizeof(jpeg_data));
    splashl.close();
    
    cmgr = (MCompositeManager*)qApp;
    // initialize MCompositeManager
    cmgr->setSurfaceWindow(0);
    cmgr->d->prepare();
    cmgr->d->xserver_stacking.init();

    device_state = new fake_device_state();
    cmgr->ut_replaceDeviceState(device_state);

    // create a fake desktop window
    fake_desktop_window *pc = new fake_desktop_window(1000);
    pc->setOrientationAngle(270);
    addWindow(pc);
    mapWindow(pc);
    MCompositeWindow *cw = cmgr->d->windows.value(1000, 0);
    QCOMPARE(cw != 0, true);
    QCOMPARE(cw->isValid(), true);

    QRectF screen = QApplication::desktop()->availableGeometry();
    splashPixmap = QPixmap(screen.width(), screen.height());
}

void ut_splashscreen::init()
{
    w = 157;
    pc = new fake_LMT_window(w, false);
    addWindow(pc);
}

void ut_splashscreen::cleanup()
{
    cmgr->d->splashTimeout();
    cmgr->d->dismissedSplashScreens.clear();
    cmgr->d->lastDestroyedSplash = MCompositeManagerPrivate::DestroyedSplash(0, 0);

    cmgr->d->removeWindow(pc->winId());
    cmgr->d->xserver_stacking.windowDestroyed(pc->winId());
    pc->deleteLater();

    MCompositeWindow *cw = cmgr->d->windows.value(pc->winId(), 0);
    if (cw)
        cw->deleteLater();

    while (qApp->hasPendingEvents()) {
        qApp->processEvents();
        usleep(100);
    }
}

void ut_splashscreen::requestSplash(const QString& pid, const QString& wmClass,
                                    const QString& pPixmap, const QString& lPixmap,
                                    const QString &pixmapId)
{
    int len = pid.length() + 1
            + wmClass.length() + 1
            + pPixmap.length() + 1
            + lPixmap.length() + 1
            + pixmapId.length() + 1;

    char buffer[len];
    char *it = buffer;

    strcpy(it, qPrintable(pid));
    it = it + pid.length() + 1;
    strcpy(it, qPrintable(wmClass));
    it = it + wmClass.length() + 1;
    strcpy(it, qPrintable(pPixmap));
    it = it + pPixmap.length() + 1;
    strcpy(it, qPrintable(lPixmap));
    it = it + lPixmap.length() + 1;
    strcpy(it, qPrintable(pixmapId));

    XChangeProperty(QX11Info::display(), cmgr->d->wm_window,
                    ATOM(_MEEGO_SPLASH_SCREEN), XA_STRING, 8, PropModeReplace,
                    (unsigned char *)buffer, len);

    XSync(QX11Info::display(), false);
}

// Splash screen should be created as expected and destroyed on timeout.
void ut_splashscreen::showSplashScreen()
{
    QVERIFY(!cmgr->d->splash);

    unsigned int pid = 123;
    QString portrait("portrait");
    QString landscape("landscape");
    requestSplash(QString::number(pid), "", portrait, landscape,
                  QString::number(splashPixmap.handle()));

    while (qApp->hasPendingEvents()) {
        qApp->processEvents();
        usleep(100);
    }

    QVERIFY(cmgr->d->splash);
    MSplashScreen *splash = cmgr->d->splash;
    QCOMPARE(splash->propertyCache()->pid(), pid);
    QCOMPARE(splash->portrait_file, portrait);
    QCOMPARE(splash->landscape_file, landscape);
    QCOMPARE(splash->pixmap, (unsigned int)splashPixmap.handle());
    QCOMPARE(cmgr->d->lastDestroyedSplash.pid, (unsigned)0);
    QCOMPARE(cmgr->d->lastDestroyedSplash.window, (Window)0);

    cmgr->d->splashTimeout();
    QVERIFY(!cmgr->d->splash);
    QCOMPARE(cmgr->d->lastDestroyedSplash.pid, pid);
    QCOMPARE(cmgr->d->lastDestroyedSplash.window, splash->window());

    QTest::qWait(10); // run idle handlers
    QVERIFY(!cmgr->d->compositing);
}

// After the fade animation the splash screen must be gone.
void ut_splashscreen::testFade()
{
    unsigned int pid = 123;

    pc->setProcessId(pid);

    requestSplash(QString::number(pid), "", "", "",
                  QString::number(splashPixmap.handle()));

    while (qApp->hasPendingEvents()) {
        qApp->processEvents();
        usleep(100);
    }

    MSplashScreen *splash = cmgr->d->splash;
    QVERIFY(splash);
    QVERIFY(!splash->windowAnimator()->pendingAnimation());

    // create a fake MapNotify event
    QVERIFY(!pc->isMapped());
    mapWindow(pc);

    QVERIFY(cmgr->d->splash);

    MCompositeWindow *cw = cmgr->d->windows.value(w, 0);
    QVERIFY(cw);
    cw->damageReceived();
    cw->damageReceived();

    splash->windowAnimator()->startTransition();
    QTest::qWait(1000); // wait for transition to finish

    QVERIFY(!cmgr->d->splash);
    QCOMPARE(pc->windowState(), NormalState);

    QVERIFY(!cmgr->d->compositing);
}

// When a splash screen has been minimized the matching application has to
// be forcefully iconified once it is mapped.
void ut_splashscreen::testIconifyInMapRequestEvent()
{
    unsigned int pid = 123;

    pc->setProcessId(pid);

    QVERIFY(!pc->stackedUnmapped());
    QCOMPARE(pc->windowState(), NormalState);

    MCompositeManagerPrivate::DismissedSplash &ds = cmgr->d->dismissedSplashScreens[pid];
    QVERIFY(!ds.blockTimer.isValid());

    XMapRequestEvent mre;
    memset(&mre, 0, sizeof(mre));
    mre.window = pc->winId();

    cmgr->d->mapRequestEvent(&mre);
    QVERIFY(pc->stackedUnmapped());
    QCOMPARE(pc->windowState(), IconicState);
    QVERIFY(ds.blockTimer.isValid());

    // make sure state is still iconic after the map event
    XMapEvent me;
    memset(&me, 0, sizeof(me));
    me.window = pc->winId();
    me.event = QX11Info::appRootWindow();
    cmgr->d->mapEvent(&me);

    QCOMPARE(pc->windowState(), IconicState);

    QTest::qWait(10);
    QVERIFY(!cmgr->d->compositing);
}

// When the splash screen is already dismissed mapEvent() has to make sure
// that the block timer is also started.
void ut_splashscreen::testTimerStartedInMapEvent()
{
    unsigned int pid = 123;

    pc->setProcessId(pid);

    MCompositeManagerPrivate::DismissedSplash &ds = cmgr->d->dismissedSplashScreens[pid];
    QVERIFY(!ds.blockTimer.isValid());

    pc->setNoAnimations(1);
    mapWindow(pc);

    QVERIFY(ds.blockTimer.isValid());

    QTest::qWait(1000);
    QVERIFY(!cmgr->d->compositing);
}

// fakes a swipe animation which might stack the splash screen to the bottom
// once it is terminated
class StackBelowAnimation: public QPropertyAnimation
{
public:
    StackBelowAnimation(QObject *parent)
        : QPropertyAnimation(parent)
    {
        setPropertyName("pos");
    }
protected:
    void updateState(QAbstractAnimation::State newState,
                     QAbstractAnimation::State oldState)
    {
        if (newState == QAbstractAnimation::Stopped &&
                oldState == QAbstractAnimation::Running)
        {
            MCompositeManager *cmgr = (MCompositeManager*)qApp;
            MCompositeWindow *cw = qobject_cast<MCompositeWindow*>(targetObject());
            cmgr->positionWindow(cw->window(),
                                 MCompositeManager::STACK_BOTTOM);
        }         QPropertyAnimation::updateState(newState, oldState);
    }
};

// Usecase: Swipe is going on right now when window is mapped.
// setting up the fade animation cancels the swipe animation
// and the splash screen is stacked to the bottom.
// Cross fade must be canceled and the splash screen must be gone.
void ut_splashscreen::testSplashDismissalWhenSettingUpCrossFade()
{
    unsigned int pid = 123;

    pc->setProcessId(pid);

    requestSplash(QString::number(pid), "", "", "",
                  QString::number(splashPixmap.handle()));

    while (qApp->hasPendingEvents()) {
        qApp->processEvents();
        usleep(100);
    }

    QVERIFY(!cmgr->d->dismissedSplashScreens.contains(pid));

    MSplashScreen *splash = cmgr->d->splash;
    QVERIFY(splash);
    splash->windowAnimator()->animationGroup()->clear();
    StackBelowAnimation *sba = new StackBelowAnimation(this);
    sba->setDuration(99999);
    sba->setTargetObject(splash);
    sba->setStartValue(splash->pos());
    sba->setEndValue(splash->pos());
    splash->windowAnimator()->animationGroup()->addAnimation(sba);
    splash->windowAnimator()->animationGroup()->start(QAbstractAnimation::DeleteWhenStopped);

    // create a fake MapNotify event
    mapWindow(pc);

    MCompositeWindow *cw = cmgr->d->windows.value(w, 0);
    QVERIFY(w);
    QCOMPARE(cw, cmgr->d->waiting_damage.data());
    QCOMPARE(pc->windowState(), NormalState);

    QVERIFY(cmgr->d->splash);
    cw->damageReceived();
    cw->damageReceived();
    QVERIFY(!cmgr->d->splash);
    QVERIFY(cmgr->d->dismissedSplashScreens.contains(pid));
    MCompositeManagerPrivate::DismissedSplash &ds = cmgr->d->dismissedSplashScreens[pid];
    QVERIFY(ds.blockTimer.isValid());
    QCOMPARE(sba->state(), QAbstractAnimation::Stopped);
    QCOMPARE(pc->windowState(), IconicState);

    QTest::qWait(1000);
    QVERIFY(!cmgr->d->compositing);
}

// When a splash screen is stacked to the bottom after splashTimeout() has been
// called the matching block timer has to be started.
void ut_splashscreen::testTimerStartingWhenDismissedSplashScreenIsStackedToBottom()
{
    unsigned int pid = 123;
    Window w = 123;

    QVERIFY(!cmgr->d->dismissedSplashScreens.contains(pid));
    cmgr->d->lastDestroyedSplash = MCompositeManagerPrivate::DestroyedSplash(w, pid);

    cmgr->d->positionWindow(w, false);

    QVERIFY(cmgr->d->dismissedSplashScreens.contains(pid));
    MCompositeManagerPrivate::DismissedSplash &ds = cmgr->d->dismissedSplashScreens[pid];
    QVERIFY(ds.blockTimer.isValid());
    QTest::qWait(1000);
    QVERIFY(!cmgr->d->compositing);
}

void ut_splashscreen::testStackingSplashBelow_data()
{
    QTest::addColumn<bool>("windowMapped");
    QTest::addColumn<bool>("timerToBeStarted");
    QTest::newRow("mapped") << true << true;
    QTest::newRow("not mapped") << false << false;
}

// When a non-dimsissed splash screen is stacked to the bottom an entry
// must be created in dismissedSplashScreens and the splash screen must time out.
void ut_splashscreen::testStackingSplashBelow()
{
    QFETCH(bool, windowMapped);
    QFETCH(bool, timerToBeStarted);

    unsigned int pid = 123;

    pc->setIsMapped(windowMapped);
    pc->setProcessId(pid);

    requestSplash(QString::number(pid), "", "", "",
                  QString::number(splashPixmap.handle()));

    while (qApp->hasPendingEvents()) {
        qApp->processEvents();
        usleep(100);
    }

    QVERIFY(cmgr->d->splash);
    QVERIFY(!cmgr->d->dismissedSplashScreens.contains(pid));

    cmgr->d->positionWindow(cmgr->d->splash->window(), false);

    QVERIFY(!cmgr->d->splash);
    QVERIFY(cmgr->d->dismissedSplashScreens.contains(pid));
    MCompositeManagerPrivate::DismissedSplash &ds = cmgr->d->dismissedSplashScreens[pid];
    QCOMPARE(ds.blockTimer.isValid(), timerToBeStarted);

    QTest::qWait(1000);
    QVERIFY(!cmgr->d->compositing);
}

class CompositeWindowAnimationProxy : public MCompositeWindowAnimation
{
public:
    CompositeWindowAnimationProxy(QObject* parent)
        : MCompositeWindowAnimation(parent),
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

// When killing an app after dismissing a splash screen we might run into a
// situation where a non-mapped window is tried to be shown right before it
// is gone for good.
// Verify that no animation is triggered in this case.
void ut_splashscreen::testFadingUnmappedWindow()
{
    MCompositeWindow *cw = new MTexturePixmapItem(w, pc);
    QVERIFY(!cw->isMapped());

    CompositeWindowAnimationProxy* an = new CompositeWindowAnimationProxy(0);
    an->setTargetWindow(cw);

    QCOMPARE(an->triggered, MCompositeWindowAnimation::NoAnimation);

    cw->q_fadeIn();

    QCOMPARE(an->triggered, MCompositeWindowAnimation::NoAnimation);

    QVERIFY(!cmgr->d->compositing);
    delete cw;
}

// A XConfigureRequestEvent to raise a window must not stack an app to the top if
// there is a matching dismissed splash entry. Additionally the block timer must
// be started if this did not happen yet.
// On the opposite when there is no dismissed splash entry the window must be
// stacked on top.
void ut_splashscreen::testConfigureWindow()
{
    unsigned int pid = 123;

    pc->setProcessId(pid);

    pc->setNoAnimations(true);
    // create a fake MapNotify event
    mapWindow(pc);
    pc->setNoAnimations(false);

    MCompositeWindow *cw = cmgr->d->windows.value(w, 0);
    QVERIFY(cw);


    QVERIFY(!cmgr->d->dismissedSplashScreens.contains(pid));

    // make sure the window is stacked to the bottom initially
    cmgr->d->positionWindow(w, false);

    // at least the desktop window and our window must be there
    QVERIFY(cmgr->d->stacking_list.size() > 1);
    QCOMPARE(cmgr->d->stacking_list.first(), w);

    XConfigureRequestEvent e;
    memset(&e, 0, sizeof(e));

    e.window = w;
    e.parent = RootWindow(QX11Info::display(), 0);
    e.value_mask |= CWStackMode;
    e.detail = Above;

    cmgr->d->configureRequestEvent(&e);

    // must be stacked on top now
    QCOMPARE(cmgr->d->stacking_list.last(), w);

    // same game but now there is a dismissed splash entry
    cmgr->d->positionWindow(w, false);
    QCOMPARE(cmgr->d->stacking_list.first(), w);
    // a splash screen has been dismissed
    MCompositeManagerPrivate::DismissedSplash &ds = cmgr->d->dismissedSplashScreens[pid];
    QVERIFY(!ds.blockTimer.isValid());

    cmgr->d->configureRequestEvent(&e);

    // stacking must not be changed
    QCOMPARE(cmgr->d->stacking_list.first(), w);
    QVERIFY(ds.blockTimer.isValid());

    while (qApp->hasPendingEvents()) {
        qApp->processEvents();
        usleep(100);
    }
    QVERIFY(!cmgr->d->compositing);
}

void ut_splashscreen::testNetActiveWindowMessage_data()
{
    QTest::addColumn<bool>("splashDismissed");
    QTest::addColumn<bool>("startTimer");
    QTest::addColumn<int>("timerRuntime");
    QTest::addColumn<bool>("shouldBeStackedToBottom");
    QTest::addColumn<bool>("shouldNotBeDismissedAfterwards");
    QTest::addColumn<bool>("timerShouldBeRunningAfterwards");

    QTest::newRow("no dismissed")
            << false << false << 0 << false << true << false;
    QTest::newRow("dismissed timer not started")
            << true << false << 0 << true << false << true;
    QTest::newRow("dismissed timer just started")
            << true << true << 0 << true << false << true;
    QTest::newRow("dismissed timer running some time")
            << true << true << 1500 << false << true << false;
}

void ut_splashscreen::testNetActiveWindowMessage()
{
    QFETCH(bool, splashDismissed);
    QFETCH(bool, startTimer);
    QFETCH(int, timerRuntime);
    QFETCH(bool, shouldBeStackedToBottom);
    QFETCH(bool, shouldNotBeDismissedAfterwards);
    QFETCH(bool, timerShouldBeRunningAfterwards);
    unsigned int pid = 123;

    pc->setIsMapped(true);
    pc->setProcessId(pid);
    // disable animations to make sure stacking is applied immediately
    pc->setNoAnimations(1);
    addWindow(pc);

    QVERIFY(!cmgr->d->dismissedSplashScreens.contains(pid));

    MCompositeManagerPrivate::DismissedSplash *ds;
    if (splashDismissed) {
        ds = &cmgr->d->dismissedSplashScreens[pid];
    }

    // make sure the window is stacked to the bottom initially
    cmgr->d->positionWindow(w, false);
    QCOMPARE(cmgr->d->stacking_list.first(), w);
    // at least the desktop window and our window must be there
    QVERIFY(cmgr->d->stacking_list.size() > 1);

    XClientMessageEvent e;
    memset(&e, sizeof(e), 0);
    e.window = w;
    e.message_type = ATOM(_NET_ACTIVE_WINDOW);
    if (startTimer)
        ds->blockTimer.start();
    if (timerRuntime)
        QTest::qWait(timerRuntime);

    cmgr->d->clientMessageEvent(&e);


    if (shouldNotBeDismissedAfterwards)
        QVERIFY(!cmgr->d->dismissedSplashScreens.contains(pid));
    else
        QCOMPARE(ds->blockTimer.isValid(), timerShouldBeRunningAfterwards);

    if (shouldBeStackedToBottom)
        QCOMPARE(cmgr->d->stacking_list.first(), w);
    else
        QCOMPARE(cmgr->d->stacking_list.last(), w);

    cmgr->d->positionWindow(w, false);

    QTest::qWait(10);
    QVERIFY(!cmgr->d->compositing);
}

// When a splash screen is closed an entry needs to be created in dismissedSplashScreens
// stating that the app has been closed and the respective PID must be SIGKILLed.
void ut_splashscreen::testCloseHandler()
{
    unsigned int pid = 123;
    MSplashScreen splash(pid, "", "", splashPixmap.handle());
    cmgr->d->lastDestroyedSplash.pid = pid;

    kill_calls.clear();

    cmgr->d->dismissedSplashScreens[pid];
    cmgr->d->closeHandler(&splash);

    QVERIFY(!cmgr->d->dismissedSplashScreens.contains(pid));

    QCOMPARE(kill_calls.size(), 2);
    QPair<pid_t, int> p(pid, SIGKILL);
    QCOMPARE(kill_calls[0], p);
    QPair<pid_t, int> p2(-pid, SIGKILL);
    QCOMPARE(kill_calls[1], p2);
}

void ut_splashscreen::testDismissedSplash()
{
    MCompositeManagerPrivate::DismissedSplash ds;

    QVERIFY(!ds.blockTimer.isValid());
    QVERIFY(ds.lifetimeTimer.isValid());
}

void ut_splashscreen::testDestroyedSplash()
{
    Window w = 123;
    unsigned pid = 456;
    MCompositeManagerPrivate::DestroyedSplash ds(w, pid);

    QCOMPARE(ds.window, w);
    QCOMPARE(ds.pid, pid);
}

void ut_splashscreen::testOrientation(const char *topedge, bool is_flat,
                                      unsigned expected_angle)
{
    QVERIFY(!cmgr->d->splash);
    device_state->fake_is_flat = is_flat;
    device_state->fake_topedge = topedge;
    qDebug("topedge: %s, %s, expected angle: %d", topedge,
           (is_flat ? "flat" : "not flat"), expected_angle);

    unsigned int pid = 123;
    QString portrait(P_SPLASH_FILE);
    QString landscape(L_SPLASH_FILE);
    requestSplash(QString::number(pid), "", portrait, landscape, 0);

    while (qApp->hasPendingEvents()) {
        qApp->processEvents();
        QTest::qWait(100);
    }

    MCompositeWindow *s = cmgr->d->splash;
    QVERIFY(s);
    QVERIFY(s->propertyCache()->orientationAngle() == expected_angle);

    cmgr->d->splashTimeout();
    QVERIFY(!cmgr->d->splash);

    QTest::qWait(10); // run idle handlers
    QVERIFY(!cmgr->d->compositing);
}

void ut_splashscreen::testSplashOrientation()
{
    // !isFlat: splash orientation taken from device orientation,
    // and unsupported orientations are translated to nearest supported one
    testOrientation("top", false, 0);
    testOrientation("left", false, 270);
    testOrientation("right", false, 0);
    testOrientation("bottom", false, 270);

    // isFlat: splash orientation taken from the desktop window
    testOrientation("top", true, 270);
    testOrientation("left", true, 270);
    testOrientation("right", true, 270);
    testOrientation("bottom", true, 270);

    // map a window in landscape orientation
    fake_LMT_window *pc = new fake_LMT_window(2000);
    pc->setOrientationAngle(0);
    addWindow(pc);
    mapWindow(pc);
    QTest::qWait(1000);
    QVERIFY(pc->isMapped());
    QVERIFY(cmgr->stackingList().indexOf(cmgr->desktopWindow())
            < cmgr->stackingList().indexOf(pc->winId()));
    QVERIFY(cmgr->d->current_app == pc->winId());

    // isFlat: splash orientation taken from the new app window
    testOrientation("top", true, 0);
    testOrientation("left", true, 0);
    testOrientation("right", true, 0);
    testOrientation("bottom", true, 0);
}

void ut_splashscreen::cleanupTestCase()
{
    QFile::remove(P_SPLASH_FILE);
    QFile::remove(L_SPLASH_FILE);
}

int main(int argc, char* argv[])
{
    // init fake but basic compositor environment
    QApplication::setGraphicsSystem("native");
    QCoreApplication::setLibraryPaths(QStringList("/usr/lib/mcompositor"));
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

    ut_splashscreen anim;

    return QTest::qExec(&anim, argc, argv);
}
