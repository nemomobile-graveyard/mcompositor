#include <QtTest/QtTest>
#include <QtGui>
#include <QGLWidget>
#include <mcompositemanager.h>
#include <mcompositemanager_p.h>
#include <mwindowpropertycache.h>
#include <mcompositewindow.h>
#include <mtexturepixmapitem.h>
#include <mcompositewindowanimation.h>
#include "ut_netClientList.h"

#include <QtDebug>

#include <X11/Xlib.h>

// fake windows should be less than the root window's value
#define VKB_1 50
#define VKB_2 60

static int dheight, dwidth;

// Skip bad window messages for mock windows
static int error_handler(Display * , XErrorEvent *)
{    
    return 0;
}

/*
static Drawable request_testpixmap()
{
    QPixmap* p = new QPixmap(1,1);
    p->fill();
    return p->handle();
}
*/

class fake_LMT_window : public MWindowPropertyCache
{
public:
    fake_LMT_window(Window w, bool is_mapped = true)
        : MWindowPropertyCache(w, &attrs)
    {
        window = w;
        memset(&attrs, 0, sizeof(attrs));
        setIsMapped(is_mapped);
        setRealGeometry(QRect(0, 0, dwidth, dheight));
        // icon geometry can be required for iconifying animation
        icon_geometry = QRect(0, 0, dwidth / 2, dheight / 2);
        type_atoms.append(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
        window_state = NormalState;
        window_type = MCompAtoms::FRAMELESS;
        has_alpha = 0;
        // mark valid to create animation object
        is_valid = true;
        cancelAllRequests();
    }
    void prependType(Atom a) { type_atoms.prepend(a);
                               window_type = MCompAtoms::INVALID; }
    void setTransientFor(Window w) { transient_for = w; }
    void setOR(bool b) { attrs.override_redirect = b; }
    void setDecorator(bool b) { is_decorator = b; }
    void setVirtual(bool b) { is_virtual = b; }
    void addToTransients(Window w) { transients.append(w); }
    void setAlpha(bool b) { has_alpha = b; }

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

    xcb_get_window_attributes_reply_t attrs;
};

void ut_netClientList::initTestCase()
{
    cmgr = (MCompositeManager*)qApp;
    cmgr->setSurfaceWindow(0);
    cmgr->d->prepare();
    cmgr->d->xserver_stacking.init();
    QVector<MWindowPropertyCache *> empty;
    prepareStack(empty);
    cmgr->d->prevNetClientListStacking.empty();
    cmgr->d->netClientList.empty();
}

void ut_netClientList::prepareStack(QVector<MWindowPropertyCache *> &t)
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

void ut_netClientList::mapWindow(MWindowPropertyCache *pc)
{
    cmgr->d->prop_caches[pc->winId()] = pc;
    cmgr->d->xserver_stacking.windowCreated(pc->winId());
    XMapEvent e;
    memset(&e, 0, sizeof(e));
    e.window = pc->winId();
    e.event = QX11Info::appRootWindow();
    cmgr->d->mapEvent(&e);
}

void ut_netClientList::unmapWindow(MWindowPropertyCache *pc)
{
    XUnmapEvent e;
    memset(&e, 0, sizeof(e));
    e.window = pc->winId();
    e.event = QX11Info::appRootWindow();
    cmgr->d->unmapEvent(&e);
}

void ut_netClientList::testDesktopMapping()
{
    fake_desktop_window *desk = new fake_desktop_window(1);
    mapWindow(desk);
    // run checkStacking()
    QTest::qWait(10);

    QCOMPARE(cmgr->d->netClientList.size() == 1, true);
    QCOMPARE(cmgr->d->netClientList.contains(1), true);
    QCOMPARE(cmgr->d->prevNetClientListStacking.size() == 1, true);
    QCOMPARE(cmgr->d->prevNetClientListStacking.contains(1), true);
}

void ut_netClientList::testAppMapping()
{
    fake_LMT_window *app = new fake_LMT_window(2);
    mapWindow(app);
    // run checkStacking()
    QTest::qWait(10);

    QCOMPARE(cmgr->d->netClientList.size() == 2, true);
    QCOMPARE(cmgr->d->netClientList[1] == 2, true);
    MCompositeWindow *w = cmgr->d->windows.value(2, 0);
    w->damageReceived();
    w->damageReceived();
    while (w->windowAnimator()->isActive())
        QTest::qWait(500); // wait the animation to finish
    QCOMPARE(cmgr->d->prevNetClientListStacking.size() == 2, true);
    QCOMPARE(cmgr->d->prevNetClientListStacking[1] == 2, true);
}

void ut_netClientList::testAppMapping2()
{
    fake_LMT_window *app = new fake_LMT_window(3);
    mapWindow(app);
    // run checkStacking()
    QTest::qWait(10);

    QCOMPARE(cmgr->d->netClientList.size() == 3, true);
    QCOMPARE(cmgr->d->netClientList[2] == 3, true);
    MCompositeWindow *w = cmgr->d->windows.value(3, 0);
    w->damageReceived();
    w->damageReceived();
    while (w->windowAnimator()->isActive())
        QTest::qWait(500); // wait the animation to finish
    QCOMPARE(cmgr->d->prevNetClientListStacking.size() == 3, true);
    QCOMPARE(cmgr->d->prevNetClientListStacking[2] == 3, true);
}

void ut_netClientList::testAppUnmapping()
{
    MCompositeWindow *w = cmgr->d->windows.value(2, 0);
    unmapWindow(w->propertyCache());
    // run checkStacking()
    QTest::qWait(10);

    QCOMPARE(cmgr->d->netClientList.size() == 2, true);
    QCOMPARE(cmgr->d->netClientList[1] == 3, true);
    QCOMPARE(cmgr->d->prevNetClientListStacking.size() == 2, true);
    QCOMPARE(cmgr->d->prevNetClientListStacking[1] == 3, true);
}

void ut_netClientList::testORMapping()
{
    fake_LMT_window *app = new fake_LMT_window(4);
    app->setOR(true);
    mapWindow(app);
    // run checkStacking()
    QTest::qWait(10);

    QCOMPARE(cmgr->d->netClientList.size() == 2, true);
    QCOMPARE(cmgr->d->netClientList[1] == 3, true);
    QCOMPARE(cmgr->d->prevNetClientListStacking.size() == 2, true);
    QCOMPARE(cmgr->d->prevNetClientListStacking[1] == 3, true);
}

void ut_netClientList::testDecoratorMapping()
{
    fake_LMT_window *app = new fake_LMT_window(5);
    app->setDecorator(true);
    mapWindow(app);
    // run checkStacking()
    QTest::qWait(10);

    QCOMPARE(cmgr->d->netClientList.size() == 2, true);
    QCOMPARE(cmgr->d->netClientList[1] == 3, true);
    QCOMPARE(cmgr->d->prevNetClientListStacking.size() == 2, true);
    QCOMPARE(cmgr->d->prevNetClientListStacking[1] == 3, true);
}

void ut_netClientList::testDockMapping()
{
    fake_LMT_window *app = new fake_LMT_window(6);
    app->prependType(ATOM(_NET_WM_WINDOW_TYPE_DOCK));
    mapWindow(app);
    // run checkStacking()
    QTest::qWait(10);

    QCOMPARE(cmgr->d->netClientList.size() == 2, true);
    QCOMPARE(cmgr->d->netClientList[1] == 3, true);
    QCOMPARE(cmgr->d->prevNetClientListStacking.size() == 2, true);
    QCOMPARE(cmgr->d->prevNetClientListStacking[1] == 3, true);
}

void ut_netClientList::testVirtualMapping()
{
    fake_LMT_window *app = new fake_LMT_window(7);
    app->setVirtual(true);
    mapWindow(app);
    // run checkStacking()
    QTest::qWait(10);

    QCOMPARE(cmgr->d->netClientList.size() == 2, true);
    QCOMPARE(cmgr->d->netClientList[1] == 3, true);
    QCOMPARE(cmgr->d->prevNetClientListStacking.size() == 2, true);
    QCOMPARE(cmgr->d->prevNetClientListStacking[1] == 3, true);

    MCompositeWindow *w = cmgr->d->windows.value(7, 0);
    w->damageReceived();
    w->damageReceived();
    while (w->windowAnimator()->isActive())
        QTest::qWait(500); // wait the animation to finish

    QCOMPARE(cmgr->d->netClientList.size() == 2, true);
    QCOMPARE(cmgr->d->netClientList[1] == 3, true);
    QCOMPARE(cmgr->d->prevNetClientListStacking.size() == 2, true);
    QCOMPARE(cmgr->d->prevNetClientListStacking[1] == 3, true);
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

    ut_netClientList test;

    return QTest::qExec(&test);
}
