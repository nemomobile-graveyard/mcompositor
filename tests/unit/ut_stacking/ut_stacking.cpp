#include <QtTest/QtTest>
#include <QtGui>
#include <QGLWidget>
#include <mcompositemanager.h>
#include <mcompositemanager_p.h>
#include <mwindowpropertycache.h>
#include "ut_stacking.h"

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
        : MWindowPropertyCache(None, &attrs)
    {
        window = w;
        memset(&attrs, 0, sizeof(attrs));
        setIsMapped(is_mapped);
        setRealGeometry(QRect(0, 0, dwidth, dheight));
        type_atoms.append(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
        window_state = NormalState;
        has_alpha = 0;
    }

    xcb_get_window_attributes_reply_t attrs;

    friend class ut_Stacking;
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

void ut_Stacking::initTestCase()
{
    cmgr = (MCompositeManager*)qApp;
}

// Ensure that windows are in the order specified by t.
// The last element of t must be zero.
bool ut_Stacking::checkOrder(Window t[])
{
    for (int i = 0, pos = 0; t[i]; ++i) {
        pos = cmgr->d->stacking_list.indexOf(t[i], pos);
        if (pos < 0)
            return false;
        ++pos;
    }
    return true;
}

void ut_Stacking::prepareStack(QVector<MWindowPropertyCache *> &t)
{
    cmgr->d->stacking_list.clear();
    cmgr->d->prop_caches.clear();
    for (int i = 0; i < t.size(); ++i) {
        cmgr->d->stacking_list.append(t[i]->winId());
        cmgr->d->prop_caches[t[i]->winId()] = t[i];
        if (t[i]->windowType() == MCompAtoms::DESKTOP)
            cmgr->d->stack[DESKTOP_LAYER] = t[i]->winId();
    }
}

void ut_Stacking::testLotOfUnmapped()
{    
    fake_desktop_window desk(1);
    fake_LMT_window normal_lmt(2), iconic_lmt(3), trans1(4), meego1(5),
                    sysmodal(6), sysdlg(7), banner(8), or_window(9),
                    trans2(10);
    Window free_id;

    trans1.transient_for = 2;
    meego1.meego_layer = 1;
    sysmodal.type_atoms.prepend(ATOM(_NET_WM_WINDOW_TYPE_DIALOG));
    sysmodal.net_wm_state.append(ATOM(_NET_WM_STATE_MODAL));
    sysdlg.type_atoms.prepend(ATOM(_NET_WM_WINDOW_TYPE_DIALOG));
    banner.type_atoms[0] = ATOM(_NET_WM_WINDOW_TYPE_NOTIFICATION);
    or_window.attrs.override_redirect = 1;
    trans2.transient_for = 3;

    // make a stack with a varying number of unmapped windows between mapped ones
    for (int n_unmapped = 1; n_unmapped < 51; ++n_unmapped) {
        Window first_unmapped;
        free_id = first_unmapped = 11;
        normal_lmt.setIsMapped(true);
        iconic_lmt.window_state = trans2.window_state = IconicState;

        QVector<MWindowPropertyCache *> stack;
        for (int i = 0; i < n_unmapped; ++i)
            stack.append(new fake_LMT_window(free_id++, false));
        stack.append(&desk);
        for (int i = 0; i < n_unmapped; ++i)
            stack.append(new fake_LMT_window(free_id++, false));
        stack.append(&meego1);
        for (int i = 0; i < n_unmapped; ++i)
            stack.append(new fake_LMT_window(free_id++, false));
        stack.append(&or_window);
        for (int i = 0; i < n_unmapped; ++i)
            stack.append(new fake_LMT_window(free_id++, false));
        stack.append(&sysmodal);
        for (int i = 0; i < n_unmapped; ++i)
            stack.append(new fake_LMT_window(free_id++, false));
        stack.append(&banner);
        for (int i = 0; i < n_unmapped; ++i)
            stack.append(new fake_LMT_window(free_id++, false));
        stack.append(&normal_lmt);
        for (int i = 0; i < n_unmapped; ++i)
            stack.append(new fake_LMT_window(free_id++, false));
        stack.append(&sysdlg);
        for (int i = 0; i < n_unmapped; ++i)
            stack.append(new fake_LMT_window(free_id++, false));
        stack.append(&iconic_lmt);
        for (int i = 0; i < n_unmapped; ++i)
            stack.append(new fake_LMT_window(free_id++, false));
        stack.append(&trans1);
        for (int i = 0; i < n_unmapped; ++i)
            stack.append(new fake_LMT_window(free_id++, false));
        stack.append(&trans2);
        for (int i = 0; i < n_unmapped; ++i)
            stack.append(new fake_LMT_window(free_id++, false));

        prepareStack(stack);
        //qDebug() << "before:" << cmgr->d->stacking_list;
        cmgr->d->roughSort();
        //qDebug() << "after:" << cmgr->d->stacking_list;
        printf("%d ", stack.size());
        fflush(stdout);

        Window correct[] = { 11, 3, 10, 1, 2, 4, 7, 6, 5, 9, 8, 0 };
        QCOMPARE(checkOrder(correct), true);

        // activate the iconic window
        int wi = cmgr->d->stacking_list.indexOf(3);
        cmgr->d->stacking_list.move(wi, cmgr->d->stacking_list.size() - 1);
        iconic_lmt.window_state = trans2.window_state = NormalState;
        cmgr->d->roughSort();
        Window correct2[] = { 11, 1, 2, 4, 7, 3, 10, 6, 5, 9, 8, 0 };
        QCOMPARE(checkOrder(correct2), true);

        // unmap the transient parent (no change in order)
        normal_lmt.setIsMapped(false);
        cmgr->d->roughSort();
        QCOMPARE(checkOrder(correct2), true);

        // drop meego level to 0 and then raise it back to 1
        meego1.meego_layer = 0;
        cmgr->d->roughSort();
        Window correct3[] = { 11, 1, 2, 4, 7, 3, 10, 5, 6, 9, 8, 0 };
        QCOMPARE(checkOrder(correct3), true);

        meego1.meego_layer = 1;
        cmgr->d->roughSort();
        QCOMPARE(checkOrder(correct2), true);

        // check that unmapped windows are in the same order
        int n_mapped = first_unmapped - 1;
        int total_unmapped = (n_mapped + 1) * n_unmapped;
        Window unmapped[total_unmapped + 1];
        for (int i = 0; i < total_unmapped; ++i)
            unmapped[i] = first_unmapped + i;
        unmapped[total_unmapped] = 0;
        QCOMPARE(checkOrder(unmapped), true);
    }
    printf("\n");
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

    ut_Stacking stacking;

    return QTest::qExec(&stacking);
}
