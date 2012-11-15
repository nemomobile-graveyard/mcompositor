#include <QtTest/QtTest>
#include <QtGui>
#include <QGLWidget>
#include <mcompositemanager.h>
#include <mcompositemanager_p.h>
#include <mwindowpropertycache.h>
#include <mcompositewindow.h>
#include "ut_propcache.h"

#include <QtDebug>

#include <X11/Xlib.h>

static int dheight, dwidth;

// Skip bad window messages for mock windows
static int error_handler(Display * , XErrorEvent *)
{    
    return 0;
}

class xcbRequest
{
public:
    xcb_get_property_reply_t reply;
    CARD32 value;
};

static unsigned next_seq = 1;
static unsigned next_winid = 1;
static unsigned reply_format = 32;
static QHash<unsigned, xcbRequest> cookieToRequest;
// values for atoms (common for all windows)
static QHash<Atom, int> propertyValue;

// fake XCB functions in order to feed test input
xcb_get_property_reply_t *xcb_get_property_reply(xcb_connection_t *conn,
                                                 xcb_get_property_cookie_t c,
                                                 xcb_generic_error_t **e)
{
    Q_UNUSED(conn);
    Q_UNUSED(e);
    //printf("fake %s, cookie %u\n", __func__, c.sequence);
    xcbRequest &r = cookieToRequest[c.sequence];
    xcb_get_property_reply_t *copy = (xcb_get_property_reply_t*)
                                       malloc(sizeof(xcb_get_property_reply_t));
    memcpy(copy, &r.reply, sizeof(xcb_get_property_reply_t));
    return copy;
}

void *xcb_get_property_value(const xcb_get_property_reply_t *r)
{
    //printf("fake %s, cookie %u\n", __func__, r->sequence);
    xcbRequest &req = cookieToRequest[r->sequence];
    return &req.value;
}

xcb_get_property_cookie_t xcb_get_property(xcb_connection_t *c,
                                           uint8_t _delete,
                                           xcb_window_t window,
                                           xcb_atom_t property,
                                           xcb_atom_t type,
                                           uint32_t long_offset,
                                           uint32_t long_length)
{
    Q_UNUSED(c);
    Q_UNUSED(_delete);
    Q_UNUSED(window);
    Q_UNUSED(type);
    Q_UNUSED(long_offset);
    Q_UNUSED(long_length);
    unsigned seq = next_seq++;
    //printf("fake %s, window %d, cookie %u\n", __func__, window, seq);
    if (!propertyValue.contains(property)) {
        cookieToRequest[seq].value = 0;
        cookieToRequest[seq].reply.format = 0;
        cookieToRequest[seq].reply.value_len = 0;
    } else {
        cookieToRequest[seq].value = propertyValue[property];
        cookieToRequest[seq].reply.format = reply_format;
        cookieToRequest[seq].reply.value_len = 1;
    }
    cookieToRequest[seq].reply.sequence = seq;
    xcb_get_property_cookie_t cookie = { seq };
    return cookie;
}

// initialized attrs for MWindowPropertyCache's constructor
static xcb_get_window_attributes_reply_t static_attrs;

class fake_LMT_window : public MWindowPropertyCache
{
public:
    fake_LMT_window(Window w, bool is_mapped = true)
        : MWindowPropertyCache(w, &static_attrs)
    {
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

    xcb_get_window_attributes_reply_t fake_attrs;

    friend class ut_PropCache;
};

void ut_PropCache::initTestCase()
{
    cmgr = (MCompositeManager*)qApp;
    cmgr->setSurfaceWindow(0);
    cmgr->ut_prepare();
}

void ut_PropCache::propertyEvent(Atom atom, Window window, int state)
{
    XPropertyEvent p;
    p.send_event = False;
    p.display = QX11Info::display();
    p.type = PropertyNotify;
    p.window = window;
    p.atom = atom;
    p.state = state;
    p.time = CurrentTime;
    cmgr->d->propertyEvent(&p);
}

void ut_PropCache::testVoidArgFunc(const char *fname, const char *rtype,
                                   Atom atom, int rsize, unsigned set_val,
                                   unsigned expected_set_val,
                                   unsigned expected_unset_val)
{
    // window with property value of set_val
    propertyValue[atom] = set_val;
    fake_LMT_window *pc = new fake_LMT_window(next_winid++);
    QVERIFY(cmgr->ut_addWindow(pc));
    const QMetaObject *m = pc->metaObject();
    QMetaMethod mm = m->method(m->indexOfMethod(fname));
    unsigned int retval = 0xdeadbeef;
    mm.invoke(pc, Qt::DirectConnection,
              QGenericReturnArgument(rtype, &retval));
    // gotta use memcmp since the return value's signedness varies
    QVERIFY(!memcmp(&retval, &expected_set_val, rsize));

    // delete it
    propertyValue.remove(atom);
    propertyEvent(atom, pc->winId(), PropertyDelete);
    mm.invoke(pc, Qt::DirectConnection,
              QGenericReturnArgument(rtype, &retval));
    QVERIFY(!memcmp(&retval, &expected_unset_val, rsize));

    // window without the property
    propertyValue.remove(atom);
    fake_LMT_window *pc2 = new fake_LMT_window(next_winid++);
    QVERIFY(cmgr->ut_addWindow(pc2));
    mm.invoke(pc2, Qt::DirectConnection,
              QGenericReturnArgument(rtype, &retval));
    QVERIFY(!memcmp(&retval, &expected_unset_val, rsize));

    // change it to set_val
    propertyValue[atom] = set_val;
    propertyEvent(atom, pc2->winId(), PropertyNewValue);
    mm.invoke(pc2, Qt::DirectConnection,
              QGenericReturnArgument(rtype, &retval));
    QVERIFY(!memcmp(&retval, &expected_set_val, rsize));

    // delete it
    propertyValue.remove(atom);
    propertyEvent(atom, pc2->winId(), PropertyDelete);
    mm.invoke(pc2, Qt::DirectConnection,
              QGenericReturnArgument(rtype, &retval));
    QVERIFY(!memcmp(&retval, &expected_unset_val, rsize));

    // window with property value of 0
    propertyValue[atom] = 0;
    fake_LMT_window *pc3 = new fake_LMT_window(next_winid++);
    QVERIFY(cmgr->ut_addWindow(pc3));
    mm.invoke(pc3, Qt::DirectConnection,
              QGenericReturnArgument(rtype, &retval));
    QVERIFY(!memcmp(&retval, &expected_unset_val, rsize));

    // change it to set_val
    propertyValue[atom] = set_val;
    propertyEvent(atom, pc3->winId(), PropertyNewValue);
    mm.invoke(pc3, Qt::DirectConnection,
              QGenericReturnArgument(rtype, &retval));
    QVERIFY(!memcmp(&retval, &expected_set_val, rsize));

    // delete it
    propertyValue.remove(atom);
    propertyEvent(atom, pc3->winId(), PropertyDelete);
    mm.invoke(pc3, Qt::DirectConnection,
              QGenericReturnArgument(rtype, &retval));
    QVERIFY(!memcmp(&retval, &expected_unset_val, rsize));
}

void ut_PropCache::testIsDecorator()
{
    testVoidArgFunc("isDecorator()", "bool", ATOM(_MEEGOTOUCH_DECORATOR_WINDOW),
                    sizeof(bool));
}

void ut_PropCache::testTransientFor()
{
    testVoidArgFunc("transientFor()", "Window",
                    ATOM(WM_TRANSIENT_FOR));
}

void ut_PropCache::testInvokedBy()
{
    testVoidArgFunc("invokedBy()", "Window",
                    ATOM(_MEEGOTOUCH_WM_INVOKED_BY));
}

void ut_PropCache::testMeegoStackingLayer()
{
    testVoidArgFunc("meegoStackingLayer()", "unsigned int",
                    ATOM(_MEEGO_STACKING_LAYER));
}

void ut_PropCache::testLowPowerMode()
{
    testVoidArgFunc("lowPowerMode()", "unsigned int",
                    ATOM(_MEEGO_LOW_POWER_MODE));
}

void ut_PropCache::testOpaqueWindow()
{
    testVoidArgFunc("opaqueWindow()", "unsigned int",
                    ATOM(_MEEGOTOUCH_OPAQUE_WINDOW));
}

void ut_PropCache::testOrientationAngle()
{
    testVoidArgFunc("orientationAngle()", "unsigned int",
                    ATOM(_MEEGOTOUCH_ORIENTATION_ANGLE));
}

void ut_PropCache::testAlwaysMapped()
{
    testVoidArgFunc("alwaysMapped()", "int",
                    ATOM(_MEEGOTOUCH_ALWAYS_MAPPED));
}

void ut_PropCache::testCannotMinimize()
{
    testVoidArgFunc("cannotMinimize()", "int",
                    ATOM(_MEEGOTOUCH_CANNOT_MINIMIZE));
}

void ut_PropCache::testPid()
{
    testVoidArgFunc("pid()", "unsigned int", ATOM(_NET_WM_PID));
}

void ut_PropCache::testGlobalAlpha()
{
    testVoidArgFunc("globalAlpha()", "int", ATOM(_MEEGOTOUCH_GLOBAL_ALPHA),
                    sizeof(int), 0xcccccd00, 0xcc, 0xff);
}

void ut_PropCache::testVideoGlobalAlpha()
{
    testVoidArgFunc("videoGlobalAlpha()", "int", ATOM(_MEEGOTOUCH_VIDEO_ALPHA),
                    sizeof(int), 0xcccccd00, 0xcc, 0xff);
}

void ut_PropCache::testNoAnimations()
{
    testVoidArgFunc("noAnimations()", "unsigned int",
                    ATOM(_MEEGOTOUCH_NO_ANIMATIONS));
}

void ut_PropCache::testVideoOverlay()
{
    reply_format = 8;
    testVoidArgFunc("videoOverlay()", "int", ATOM(_OMAP_VIDEO_OVERLAY));
    reply_format = 32;
}

void ut_PropCache::testWindowTypes()
{
    // regular LMT app
    fake_LMT_window *LMT = new fake_LMT_window(next_winid++);
    LMT->cancelAllRequests(); // don't try to read from the non-existing window
    QVERIFY(cmgr->ut_addWindow(LMT, false));
    LMT->type_atoms.clear();
    LMT->type_atoms.append(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
    LMT->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
    QVERIFY(LMT->windowType() == MCompAtoms::FRAMELESS);
    QVERIFY(LMT->windowTypeAtom() == ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
    QVERIFY(LMT->isAppWindow());
    QVERIFY(LMT->isAppWindow(true));

    // transient LMT app
    propertyValue[ATOM(WM_TRANSIENT_FOR)] = LMT->winId();
    fake_LMT_window *trLMT = new fake_LMT_window(next_winid++);
    trLMT->transientFor();  // read transient_for before the cancel
    trLMT->cancelAllRequests();
    QVERIFY(cmgr->ut_addWindow(trLMT, false));
    trLMT->type_atoms.clear();
    trLMT->type_atoms.append(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
    trLMT->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
    QVERIFY(trLMT->windowType() == MCompAtoms::FRAMELESS);
    QVERIFY(trLMT->transientFor() == LMT->winId());
    QVERIFY(cmgr->ut_addWindow(LMT)); // map the parent
    QVERIFY(!trLMT->isAppWindow());  // transient not considered an app
    QVERIFY(trLMT->isAppWindow(true)); // transient considered as app
    propertyValue.remove(ATOM(WM_TRANSIENT_FOR));

    // decorated application window
    fake_LMT_window *normal = new fake_LMT_window(next_winid++);
    normal->cancelAllRequests();
    QVERIFY(cmgr->ut_addWindow(normal, false));
    normal->type_atoms.clear();
    normal->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
    QVERIFY(normal->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
    QVERIFY(normal->windowType() == MCompAtoms::NORMAL);
    QVERIFY(normal->isAppWindow());
    QVERIFY(normal->isAppWindow(true));

    // override-redirect window
    fake_LMT_window *OR = new fake_LMT_window(next_winid++);
    OR->cancelAllRequests();
    QVERIFY(cmgr->ut_addWindow(OR, false));
    OR->attrs->override_redirect = 1;
    QVERIFY(!OR->isAppWindow());
    QVERIFY(!OR->isAppWindow(true));
    QVERIFY(OR->isOverrideRedirect());

    // non-transient transient menu
    fake_LMT_window *menu = new fake_LMT_window(next_winid++);
    menu->cancelAllRequests();
    QVERIFY(cmgr->ut_addWindow(menu, false));
    menu->type_atoms.clear();
    menu->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_MENU));
    menu->type_atoms.append(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
    menu->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
    QVERIFY(menu->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_MENU));
    QVERIFY(!menu->isAppWindow());
    QVERIFY(!menu->isAppWindow(true));

    // transient menu
    QVERIFY(cmgr->ut_addWindow(menu, true)); // map the parent
    QVERIFY(menu->isMapped());
    propertyValue[ATOM(WM_TRANSIENT_FOR)] = menu->winId();
    fake_LMT_window *trmenu = new fake_LMT_window(next_winid++);
    trmenu->transientFor();  // read transient_for before the cancel
    trmenu->cancelAllRequests();
    QVERIFY(cmgr->ut_addWindow(trmenu, false));
    trmenu->type_atoms.clear();
    trmenu->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_MENU));
    trmenu->type_atoms.append(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
    trmenu->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
    QVERIFY(trmenu->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_MENU));
    QVERIFY(trmenu->transientFor() == menu->winId());
    QVERIFY(!trmenu->isAppWindow());
    QVERIFY(!trmenu->isAppWindow(true));
    propertyValue.remove(ATOM(WM_TRANSIENT_FOR));

    // Non-modal, non-transient dialog is considered app window because it's
    // stacked in the same way (this is so-called "system dialog").
    // First, a non-decorated one.
    fake_LMT_window *dlg = new fake_LMT_window(next_winid++);
    dlg->cancelAllRequests();
    QVERIFY(cmgr->ut_addWindow(dlg, false));
    dlg->type_atoms.clear();
    dlg->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_DIALOG));
    dlg->type_atoms.append(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
    dlg->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
    QVERIFY(dlg->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DIALOG));
    QVERIFY(dlg->windowType() == MCompAtoms::NO_DECOR_DIALOG);
    QVERIFY(dlg->isAppWindow());
    QVERIFY(dlg->isAppWindow(true));

    // decorated one
    fake_LMT_window *dlg2 = new fake_LMT_window(next_winid++);
    dlg2->cancelAllRequests();
    QVERIFY(cmgr->ut_addWindow(dlg2, false));
    dlg2->type_atoms.clear();
    dlg2->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_DIALOG));
    dlg2->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
    QVERIFY(dlg2->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DIALOG));
    QVERIFY(dlg2->windowType() == MCompAtoms::DIALOG);
    QVERIFY(dlg2->isAppWindow());
    QVERIFY(dlg2->isAppWindow(true));

    // non-decorated, transient dialog
    propertyValue[ATOM(WM_TRANSIENT_FOR)] = LMT->winId();
    fake_LMT_window *trdlg = new fake_LMT_window(next_winid++);
    trdlg->transientFor();
    trdlg->cancelAllRequests();
    QVERIFY(cmgr->ut_addWindow(trdlg, false));
    trdlg->type_atoms.clear();
    trdlg->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_DIALOG));
    trdlg->type_atoms.append(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
    trdlg->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
    QVERIFY(trdlg->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DIALOG));
    QVERIFY(trdlg->windowType() == MCompAtoms::NO_DECOR_DIALOG);
    QVERIFY(!trdlg->isAppWindow());
    QVERIFY(!trdlg->isAppWindow(true));
    propertyValue.remove(ATOM(WM_TRANSIENT_FOR));

    // non-transient, non-decorated, modal dialog ("system-modal dialog")
    fake_LMT_window *dlg3 = new fake_LMT_window(next_winid++);
    dlg3->cancelAllRequests();
    QVERIFY(cmgr->ut_addWindow(dlg3, false));
    dlg3->type_atoms.clear();
    dlg3->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_DIALOG));
    dlg3->type_atoms.append(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
    dlg3->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
    dlg3->net_wm_state.append(ATOM(_NET_WM_STATE_MODAL));
    QVERIFY(dlg3->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DIALOG));
    QVERIFY(dlg3->windowType() == MCompAtoms::NO_DECOR_DIALOG);
    QVERIFY(!dlg3->isAppWindow());
    QVERIFY(!dlg3->isAppWindow(true));

    // desktop window
    fake_LMT_window *desk = new fake_LMT_window(next_winid++);
    desk->cancelAllRequests();
    QVERIFY(cmgr->ut_addWindow(desk, false));
    desk->type_atoms.clear();
    desk->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_DESKTOP));
    desk->type_atoms.append(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
    desk->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
    QVERIFY(desk->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DESKTOP));
    QVERIFY(desk->windowType() == MCompAtoms::DESKTOP);
    QVERIFY(!desk->isAppWindow());
    QVERIFY(!desk->isAppWindow(true));

    // input method window
    fake_LMT_window *vkb = new fake_LMT_window(next_winid++);
    vkb->cancelAllRequests();
    QVERIFY(cmgr->ut_addWindow(vkb, false));
    vkb->type_atoms.clear();
    vkb->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_INPUT));
    QVERIFY(vkb->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_INPUT));
    QVERIFY(vkb->windowType() == MCompAtoms::INPUT);
    QVERIFY(!vkb->isAppWindow());
    QVERIFY(!vkb->isAppWindow(true));

    // decorator window
    propertyValue[ATOM(_MEEGOTOUCH_DECORATOR_WINDOW)] = 1;
    fake_LMT_window *deco = new fake_LMT_window(next_winid++);
    deco->isDecorator();
    deco->cancelAllRequests();
    QVERIFY(cmgr->ut_addWindow(deco, false));
    deco->type_atoms.clear();
    deco->type_atoms.append(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
    deco->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
    QVERIFY(deco->windowTypeAtom() == ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
    QVERIFY(deco->windowType() == MCompAtoms::FRAMELESS);
    QVERIFY(deco->isDecorator());
    QVERIFY(!deco->isAppWindow());
    QVERIFY(!deco->isAppWindow(true));
    propertyValue.remove(ATOM(_MEEGOTOUCH_DECORATOR_WINDOW));

    // notification window
    fake_LMT_window *note = new fake_LMT_window(next_winid++);
    note->cancelAllRequests();
    QVERIFY(cmgr->ut_addWindow(note, false));
    note->type_atoms.clear();
    note->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_NOTIFICATION));
    note->type_atoms.append(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
    note->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
    QVERIFY(note->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_NOTIFICATION));
    QVERIFY(note->windowType() == MCompAtoms::NOTIFICATION);
    QVERIFY(!note->isAppWindow());
    QVERIFY(!note->isAppWindow(true));

    // dock window (e.g. the quick launch bar)
    fake_LMT_window *dock = new fake_LMT_window(next_winid++);
    dock->cancelAllRequests();
    QVERIFY(cmgr->ut_addWindow(dock, false));
    dock->type_atoms.clear();
    dock->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_DOCK));
    dock->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
    QVERIFY(dock->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DOCK));
    QVERIFY(dock->windowType() == MCompAtoms::DOCK);
    QVERIFY(!dock->isAppWindow());
    QVERIFY(!dock->isAppWindow(true));

    // sheet window (application window with different animation) 
    fake_LMT_window *sheet = new fake_LMT_window(next_winid++);
    sheet->cancelAllRequests();
    QVERIFY(cmgr->ut_addWindow(sheet, false));
    sheet->type_atoms.clear();
    sheet->type_atoms.append(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
    sheet->type_atoms.append(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
    sheet->type_atoms.append(ATOM(_MEEGOTOUCH_NET_WM_WINDOW_TYPE_SHEET));
    QVERIFY(sheet->windowTypeAtom() == ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE));
    QVERIFY(sheet->windowType() == MCompAtoms::SHEET);
    QVERIFY(sheet->isAppWindow());
    QVERIFY(sheet->isAppWindow(true));
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

    ut_PropCache test;

    return QTest::qExec(&test);
}
