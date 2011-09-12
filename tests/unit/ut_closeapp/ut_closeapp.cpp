#include <QtTest/QtTest>
#include <QtGui>
#include <QGLWidget>
#include <mcompositemanager.h>
#include <mcompositemanager_p.h>
#include <mwindowpropertycache.h>
#include <mcompositewindow.h>
#include <mtexturepixmapitem.h>
#include "ut_closeapp.h"

#include <QtDebug>

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
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
        : MWindowPropertyCache(None, &attrs)
    {
        window = w;
        memset(&attrs, 0, sizeof(attrs));
        setIsMapped(is_mapped);
        setRealGeometry(QRect(0, 0, dwidth, dheight));
        type_atoms.append(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
        wm_protocols.append(ATOM(WM_DELETE_WINDOW));
        wm_protocols.append(ATOM(_NET_WM_PING));
        window_state = NormalState;
        has_alpha = 0;
    }

    xcb_get_window_attributes_reply_t attrs;

    friend class ut_CloseApp;
};

class fake_desktop_window : public MWindowPropertyCache
{
public:
    fake_desktop_window(Window w)
        : MWindowPropertyCache(None, &attrs)
    {
        window = w;
        memset(&attrs, 0, sizeof(attrs));
        setIsMapped(true);
        setRealGeometry(QRect(0, 0, dwidth, dheight));
        type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_DESKTOP));
        window_state = NormalState;
        has_alpha = 0;
    }

    xcb_get_window_attributes_reply_t attrs;
};

void ut_CloseApp::initTestCase()
{
    cmgr = (MCompositeManager*)qApp;
    cmgr->setSurfaceWindow(0);
    cmgr->d->prepare();
    cmgr->d->xserver_stacking.init();
}

void ut_CloseApp::prepareStack(QVector<MWindowPropertyCache *> &t)
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

void ut_CloseApp::testKillIconified()
{
    fake_desktop_window desk(1);
    fake_LMT_window iconic_lmt(2);
    iconic_lmt.window_state = IconicState;

    QVector<MWindowPropertyCache *> stack;
    stack.append(&iconic_lmt);
    stack.append(&desk);
    MCompositeWindow *cw = new MTexturePixmapItem(2, &iconic_lmt);
    cmgr->d->windows[2] = cw;

    prepareStack(stack);
    cmgr->d->roughSort();

    if (!(iconic_lmt.wm_pid = fork())) {
        pause();
        abort();
    }
    QCOMPARE(iconic_lmt.pid() > 0, true);

    // this should start a timer and kill the process after the timeout
    int status;
    cmgr->d->closeHandler(cw);
    QTest::qWait(cmgr->configInt("close-timeout-ms") + 500);
    QCOMPARE(waitpid(-1, &status, WNOHANG), (int)iconic_lmt.wm_pid);
    QCOMPARE(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL, true);
}

void ut_CloseApp::testCloseIconifiedWithRaise()
{
    fake_desktop_window desk(1);
    fake_LMT_window iconic_lmt(2);
    iconic_lmt.window_state = IconicState;

    QVector<MWindowPropertyCache *> stack;
    stack.append(&iconic_lmt);
    stack.append(&desk);
    MCompositeWindow *cw = new MTexturePixmapItem(2, &iconic_lmt);
    cmgr->d->windows[2] = cw;

    prepareStack(stack);
    cmgr->d->roughSort();

    if (!(iconic_lmt.wm_pid = fork())) {
        pause();
        abort();
    }
    QCOMPARE(iconic_lmt.pid() > 0, true);

    // raise the window before the timer expires and check the process
    // wasn't killed 
    cmgr->d->closeHandler(cw);
    QTest::qWait(cmgr->configInt("close-timeout-ms") / 2);

    XEvent e;
    memset(&e, 0, sizeof(e));
    e.xclient.type = ClientMessage;
    e.xclient.message_type = ATOM(_NET_ACTIVE_WINDOW);
    e.xclient.display = QX11Info::display();
    e.xclient.window = 2;
    e.xclient.format = 32;
    e.xclient.data.l[0] = 1;
    cmgr->d->clientMessageEvent(&(e.xclient));

    cmgr->d->closeHandler(cw);
    QTest::qWait(cmgr->configInt("close-timeout-ms") / 2 + 500);
    QCOMPARE(waitpid(-1, NULL, WNOHANG), 0);
    kill(iconic_lmt.wm_pid, SIGKILL);
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

    ut_CloseApp tests;

    return QTest::qExec(&tests);
}
