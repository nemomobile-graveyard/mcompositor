#include <QtTest/QtTest>
#include <QtGui>
#include <QGLWidget>
#include <mcompositemanager.h>
#include <mcompositemanager_p.h>
#include <mwindowpropertycache.h>
#include <mcompositewindow.h>
#include <mtexturepixmapitem.h>
#include <mcompositewindowanimation.h>
#include "ut_compositing.h"

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

class fake_LMT_window : public MWindowPropertyCache
{
public:
    fake_LMT_window(Window w, bool is_mapped = true)
        : MWindowPropertyCache(w, &attrs)
    {
        cancelAllRequests();
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
    }
    void prependType(Atom a) { type_atoms.prepend(a); }
    void setTransientFor(Window w) { transient_for = w; }
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

void ut_Compositing::initTestCase()
{
    cmgr = (MCompositeManager*)qApp;
    cmgr->setSurfaceWindow(0);
    cmgr->d->prepare();
    cmgr->d->xserver_stacking.init();
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

void ut_Compositing::testDesktopMapping()
{
    fake_desktop_window *desk = new fake_desktop_window(1);
    mapWindow(desk);
    MCompositeWindow *w = cmgr->d->windows.value(1, 0);
    QCOMPARE(w != 0, true);
    w->damageReceived();
    w->damageReceived();

    QCOMPARE(!cmgr->d->stacking_list.isEmpty(), true);
    QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), true);
    QCOMPARE(cmgr->d->compositing, false);
    QCOMPARE(cmgr->d->overlay_mapped, false);
    QCOMPARE(w->window() == 1, true);
    QCOMPARE(((MTexturePixmapItem*)w)->isDirectRendered(), true);
    QCOMPARE(MCompositeWindow::we_have_grab, false);
}

void ut_Compositing::testAppMapping()
{
    fake_LMT_window *app = new fake_LMT_window(2);
    mapWindow(app);
    MCompositeWindow *w = cmgr->d->windows.value(2, 0);
    QCOMPARE(w != 0, true);

    // check that it is not visible after idle handlers
    QTest::qWait(10);
    QCOMPARE(w->isVisible(), false);
    QCOMPARE(w->windowObscured(), false);

    w->damageReceived();
    w->damageReceived();
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
    QCOMPARE(cmgr->d->compositing, false);
    QCOMPARE(cmgr->d->overlay_mapped, false);
    QCOMPARE(((MTexturePixmapItem*)w)->isDirectRendered(), true);
    QCOMPARE(MCompositeWindow::we_have_grab, false);
}

// unmap an application (depends on the previous test)
void ut_Compositing::testAppUnmapping()
{
    MCompositeWindow *w = cmgr->d->windows.value(2, 0);
    QCOMPARE(w != 0, true);
    w->closeWindowRequest();
    unmapWindow(w->propertyCache());
    QCOMPARE(w->windowAnimator()->isActive(), true);
    while (w->windowAnimator()->isActive()) {
        QCOMPARE(cmgr->d->compositing, true);
        QCOMPARE(cmgr->d->overlay_mapped, true);
        QCOMPARE(((MTexturePixmapItem*)w)->isDirectRendered(), false);
        QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), false);
        QTest::qWait(500); // wait the animation to finish
    }
    QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), true);
    QCOMPARE(cmgr->d->compositing, false);
    QCOMPARE(cmgr->d->overlay_mapped, false);
    QCOMPARE(MCompositeWindow::we_have_grab, false);
}

// test re-mapping of an unmapped app (depends on the previous test)
void ut_Compositing::testAppRemapping()
{
    MCompositeWindow *w = cmgr->d->windows.value(2, 0);
    QCOMPARE(w != 0, true);
    QCOMPARE(w->isMapped(), false);
    mapWindow(w->propertyCache());

    // check that it is not visible after idle handlers
    QTest::qWait(10);
    QCOMPARE(w->isVisible(), false);
    QCOMPARE(w->windowObscured(), false);
    QCOMPARE(((MTexturePixmapItem*)w)->isDirectRendered(), false);

    w->damageReceived();
    w->damageReceived();
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
    QCOMPARE(cmgr->d->compositing, false);
    QCOMPARE(cmgr->d->overlay_mapped, false);
    QCOMPARE(((MTexturePixmapItem*)w)->isDirectRendered(), true);
    QCOMPARE(MCompositeWindow::we_have_grab, false);
}

// VKB appearing for an app when that app is showing the mapping animation
void ut_Compositing::testVkbMappingWhenAppAnimating()
{
    fake_LMT_window *app = new fake_LMT_window(3);
    mapWindow(app);
    MCompositeWindow *w = cmgr->d->windows.value(3, 0);
    QCOMPARE(w != 0, true);

    w->damageReceived();
    w->damageReceived();
    QTest::qWait(10); // get the animation started
    QCOMPARE(w->windowAnimator()->isActive(), true);

    MCompositeWindow *v;
    if (w->windowAnimator()->isActive()) {
        fake_LMT_window *vkb = new fake_LMT_window(VKB_1);
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
    QCOMPARE(cmgr->d->compositing, false);
    QCOMPARE(cmgr->d->overlay_mapped, false);
    QCOMPARE(((MTexturePixmapItem*)v)->isDirectRendered(), true);
    // self-compositing VKB requires redirected app
    QCOMPARE(((MTexturePixmapItem*)w)->isDirectRendered(), false);
    QCOMPARE(MCompositeWindow::we_have_grab, false);
}

// normal VKB mapping case
void ut_Compositing::testVkbMapping()
{
    fake_LMT_window *app = new fake_LMT_window(4);
    mapWindow(app);
    MCompositeWindow *w = cmgr->d->windows.value(4, 0);
    QCOMPARE(w != 0, true);

    w->damageReceived();
    w->damageReceived();
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
    QCOMPARE(MCompositeWindow::we_have_grab, false);
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
    QCOMPARE(w->isVisible(), true);
    QCOMPARE(w->windowObscured(), false);
    QCOMPARE(w->paintedAfterMapping(), true);
    QCOMPARE(((MTexturePixmapItem*)w)->isDirectRendered(), false);
    QCOMPARE(cmgr->d->compositing, true);
    QCOMPARE(cmgr->d->overlay_mapped, true);
    QCOMPARE(cmgr->d->possiblyUnredirectTopmostWindow(), false);

    // check the windows below
    MCompositeWindow *vkb = cmgr->d->windows.value(VKB_2, 0);
    QCOMPARE(vkb->isVisible(), true);
    QCOMPARE(vkb->windowObscured(), false);
    QCOMPARE(vkb->paintedAfterMapping(), true);
    QCOMPARE(((MTexturePixmapItem*)vkb)->isDirectRendered(), false);
    // self-compositing VKB requires unobscured and redirected app
    MCompositeWindow *app = cmgr->d->windows.value(4, 0);
    QCOMPARE(app->windowObscured(), false);
    QCOMPARE(((MTexturePixmapItem*)app)->isDirectRendered(), false);
    QCOMPARE(MCompositeWindow::we_have_grab, false);
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
    // self-compositing VKB requires unobscured and redirected app
    MCompositeWindow *app = cmgr->d->windows.value(4, 0);
    QCOMPARE(app->windowObscured(), false);
    QCOMPARE(((MTexturePixmapItem*)app)->isDirectRendered(), false);
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

    ut_Compositing test;

    return QTest::qExec(&test);
}
