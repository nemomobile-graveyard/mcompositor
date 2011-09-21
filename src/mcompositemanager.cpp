/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (directui@nokia.com)
**
** This file is part of mcompositor.
**
** If you have questions regarding the use of this file, please contact
** Nokia at directui@nokia.com.
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation
** and appearing in the file LICENSE.LGPL included in the packaging
** of this file.
**
****************************************************************************/

#include "mtexturepixmapitem.h"
#include "mtexturepixmapitem_p.h"
#include "mcompositemanager.h"
#include "mcompositemanager_p.h"
#include "mcompositescene.h"
#include "msimplewindowframe.h"
#include "mdecoratorframe.h"
#include "mdevicestate.h"
#include "mcompositemanagerextension.h"
#include "mcompmgrextensionfactory.h"
#include "mcontextproviderwrapper.h"
#include "mcompositordebug.h"
#include "msplashscreen.h"
#include "mcompositewindowanimation.h"

#include <QX11Info>
#include <QByteArray>
#include <QVector>
#include <QtPlugin>

#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h>
#include <X11/Xmd.h>
#include <X11/XKBlib.h>
#include <X11/Xproto.h>
#include "mcompatoms_p.h"

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>

#define TRANSLUCENT 0xe0000000
#define OPAQUE      0xffffffff

/*
  The reason why we have to look at the entire redirected buffers is that we
  never know if a window is physically visible or not when compositing mode is
  toggled  e.g. What if a window is partially translucent and how do we know
  that the window beneath it can be physically seen by the user and the window
  beneath that window and so on?

  But this is mitigated anyway by the fact that the items representing those
  buffers know whether they are redirected or not and will not switch to
  another state if they are already in that state. So the overhead of freeing
  and allocating EGL resources for the entire buffers is low.
 */

// own log for catching bugs
#define _log(txt, args... ) { FILE *out; out = fopen("/tmp/mcompositor.log", "a"); if(out) { fprintf(out, "" txt, ##args ); fclose(out); } }

#define COMPOSITE_WINDOW(X) MCompositeWindow::compositeWindow(X)
#define FULLSCREEN_WINDOW(X) \
        (!(X)->isDecorator() && \
         (X)->netWmState().contains(ATOM(_NET_WM_STATE_FULLSCREEN)))
#define MODAL_WINDOW(X) \
        ((X)->netWmState().contains(ATOM(_NET_WM_STATE_MODAL)))
#define DECORATED_FS_WINDOW(X) (device_state->ongoingCall() && \
                     FULLSCREEN_WINDOW(X) && \
                     X->statusbarGeometry().isEmpty() && \
                     X->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_MENU))

static KeyCode switcher_key = 0;
static bool lockscreen_painted = false;
int MCompositeManager::sighupFd[2];

static bool should_be_pinged(MCompositeWindow *cw);
static bool compareWindows(Window w_a, Window w_b);

#ifdef WINDOW_DEBUG
static QTime overhead_measure;
static bool debug_mode = false; // this can be toggled with SIGUSR1
template<class T>
static QString dumpWindows(const T &wins, bool leftToRight=true,
                           const char *sep=", ", bool prefix=false);
#endif

// Enable to see the decisions of the stacker.
#if 0
# ifndef WINDOW_DEBUG
#  define WINDOW_DEBUG // We use dumpWindows() in STACKING().
# endif
# define STACKING_DEBUGGING
# define STACKING(fmt, args...)                     \
    qDebug("line:%u: " fmt, __LINE__ ,##args)
# define STACKING_MOVE(from, to)                    \
    STACKING("moving %d (0x%lx) -> %d in stack",    \
           from,                                    \
           0 <= from && from < stacking_list.size() \
              ? stacking_list[from] : 0,            \
           to)
#else
# define STACKING(...)                              /* NOP */
# define STACKING_MOVE(...)                         /* NOP */
#endif

// Enable to see the decisions of compareWindows().
#if 0
# define SORTING(isLess, STR)                            \
    do {                                            \
        STACKING(STR " 0x%lx(%c) %s 0x%lx(%c)",                  \
               w_a, pc_a->isMapped() ? 'M' : 'U', \
                        isLess ? "<" : ">", w_b, \
                    pc_b->isMapped() ? 'M' : 'U');     \
        return isLess;                              \
    } while (0)
#else
# define SORTING(isLess, STR)                            return isLess
#endif

// Enable to see what and why getTopmostApp() chooses
// as a toplevel window.
#if 0
# define GTA(...)   qDebug("getTopmostApp: " __VA_ARGS__)
#else
# define GTA(...)                                   /* NOP */
#endif

Atom MCompAtoms::atoms[MCompAtoms::ATOMS_TOTAL];
MCompAtoms::randr_t MCompAtoms::randr;

void MCompAtoms::init()
{
    Display *dpy = QX11Info::display();
    QMetaObject const &me = staticMetaObject;

    // MCompAtoms::atoms <- intern(MCompAtoms::Atoms).
    QMetaEnum e = me.enumerator(me.indexOfEnumerator("Atoms"));
    QVector<const char *> names(ATOMS_TOTAL);
    for (int i = 0; i < ATOMS_TOTAL; i++)
        names[i] = e.valueToKey(i);
    if (!XInternAtoms(dpy, (char **)names.constData(), ATOMS_TOTAL,
                      False, atoms))
        qFatal("XInternAtoms failed");

    // root::_NET_SUPPORTED <- MCompAtoms::atoms
    XChangeProperty(dpy, QX11Info::appRootWindow(),
                    atoms[_NET_SUPPORTED],
                    XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)atoms, ATOMS_TOTAL);

    // MCompAtoms::randr <- intern(randr_names)
    static const char *randr_names[] = {
        RR_PROPERTY_CONNECTOR_TYPE, "Panel",
        "AlphaMode", "GraphicsAlpha", "VideoAlpha",
    };
    Q_ASSERT(sizeof(randr_names)/sizeof(randr_names[0])
             == sizeof(randr)/sizeof(randr.atoms[0]));
    if (!XInternAtoms(dpy, (char **)randr_names,
                      sizeof(randr_names)/sizeof(randr_names[0]),
                      False, randr.atoms))
        qFatal("XInternAtoms failed");
}

static void skiptaskbar_wm_state(int toggle, MWindowPropertyCache *pc)
{
    Atom skip = ATOM(_NET_WM_STATE_SKIP_TASKBAR);
    bool update_root = false;

    if (toggle == 2)
        toggle = !pc->netWmState().contains(skip);
    if (toggle == 1)
        update_root = pc->addToNetWmState(skip);
    if (toggle == 0)
        update_root = pc->removeFromNetWmState(skip);

    if (update_root) {
        XPropertyEvent p;
        p.send_event = True;
        p.display = QX11Info::display();
        p.type   = PropertyNotify;
        p.window = RootWindow(QX11Info::display(), 0);
        p.atom   = ATOM(_NET_CLIENT_LIST);
        p.state  = PropertyNewValue;
        p.time   = CurrentTime;
        XSendEvent(QX11Info::display(), p.window, False, PropertyChangeMask,
                   (XEvent *)&p);
    }
}

static void fullscreen_wm_state(MCompositeManagerPrivate *priv,
                                int toggle, Window window,
                                MWindowPropertyCache *pc)
{
    Atom fullscreen = ATOM(_NET_WM_STATE_FULLSCREEN);
    Display *dpy = QX11Info::display();

    if (toggle == 2)
        toggle = !pc->netWmState().contains(fullscreen);
    if (toggle == 0) {
        /* remove */
        pc->removeFromNetWmState(fullscreen);
        MCompositeWindow *win = MCompositeWindow::compositeWindow(window);
        if (win && priv->needDecoration(win->propertyCache()))
            win->setDecorated(true);
        if (win && !MDecoratorFrame::instance()->managedWindow()
            && win->needDecoration())
            priv->dirtyStacking(false); // reset decorator's managed window
        if (pc->isMapped())
            priv->dirtyStacking(false);
    }
    if (toggle == 1) {
        /* add */
        pc->addToNetWmState(fullscreen);
        int xres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
        int yres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;
        XMoveResizeWindow(dpy, window, 0, 0, xres, yres);
        MOVE_RESIZE(window, 0, 0, xres, yres);
        pc->setRequestedGeometry(QRect(0, 0, xres, yres));
        MCompositeWindow *win = priv->windows.value(window, 0);
        if (win && !priv->device_state->ongoingCall())
            win->setDecorated(false);
        if (!priv->device_state->ongoingCall()
            && MDecoratorFrame::instance()->managedWindow() == window) {
            MDecoratorFrame::instance()->setManagedWindow(0);
            STACKING("positionWindow 0x%lx -> bottom",
                     MDecoratorFrame::instance()->winId());
            priv->positionWindow(MDecoratorFrame::instance()->winId(), false);
        }
        if (pc->isMapped())
            priv->dirtyStacking(false);
    }
}

/* Finds, caches and returns the primary (Panel) RROutput.
 * Returns None if it cannot be found or it doesn't support
 * alpha blending. */
static RROutput find_primary_output()
{
    static bool been_here = false;
    static RROutput primary = None;
    int i;
    Display *dpy;
    int major, minor;
    bool has_alpha_mode;
    XRRScreenResources *scres;

    /* Initialize only once. */
    if (been_here != None)
        return primary;
    been_here = true;

    /* Check RandR, who knows what kind of X server we ride. */
    dpy = QX11Info::display();
    if (!XRRQueryVersion(dpy, &major, &minor)
        || !(major > 1 || (major == 1 && minor >= 3)))
      return None;

    /* Enumerate all the outputs X knows about and find the one
     * whose connector type is "Panel". */
    if (!(scres = XRRGetScreenResources(dpy, DefaultRootWindow(dpy))))
        return None;

    has_alpha_mode = false;
    for (i = 0, primary = None; i < scres->noutput && primary == None; i++) {
        Atom t;
        int fmt;
        unsigned char *contype;
        unsigned long nitems, rem;

        if (XRRGetOutputProperty(dpy, scres->outputs[i],
                                 MCompAtoms::randr.ctype,
                                 0, 1, False, False, AnyPropertyType, &t,
                                 &fmt, &nitems, &rem, &contype) == Success) {
            if (t == XA_ATOM && fmt == 32 && nitems == 1
                && *(Atom *)contype == MCompAtoms::randr.panel) {
                unsigned char *alpha_mode;

                /* Does the primary output support alpha blending? */
                primary = scres->outputs[i];
                if (XRRGetOutputProperty(dpy, primary,
                          MCompAtoms::randr.alpha_mode,
                          0, 1, False, False, AnyPropertyType,
                          &t, &fmt, &nitems, &rem, &alpha_mode) == Success) {
                    has_alpha_mode = t == XA_INTEGER && fmt == 32
                      && nitems == 1;
                    XFree(alpha_mode);
                }
            }
            XFree(contype);
        }
    }
    XRRFreeScreenResources(scres);

    /* If the primary output doesn't support alpha blending, don't bother. */
    if (!has_alpha_mode)
        primary = None;

    return primary;
}

/* Set GraphicsAlpha and/or VideoAlpha of the primary output
 * and enable/disable alpha blending if necessary. */
static void set_global_alpha(int new_gralpha, int new_vidalpha)
{
    static int blending = -1, gralpha = -1, vidalpha = -1;
    RROutput output;
    Display *dpy;
    int blend;

    Q_ASSERT(-1 <= new_gralpha  && new_gralpha  <= 255);
    Q_ASSERT(-1 <= new_vidalpha && new_vidalpha <= 255);
    if ((output = find_primary_output()) == None)
        return;
    dpy = QX11Info::display();

    /* Only set changed properties. */
    if (new_gralpha < 0)
        new_gralpha = gralpha;
    if (new_vidalpha < 0)
        new_vidalpha = vidalpha;
    if (new_gralpha < 0 && new_vidalpha < 0)
        /* There must have been an error getting the properties. */
        return;

    blend = new_gralpha < 255 || new_vidalpha < 255;
    if (blend != blending && !blend)
        /* Disable blending first. */
        XRRChangeOutputProperty(dpy, output,
                                MCompAtoms::randr.alpha_mode,
                                XA_INTEGER, 32, PropModeReplace,
                                (unsigned char *)&blend, 1);

    if (new_gralpha >= 0 && new_gralpha != gralpha) {
        /* Change or reset graphics alpha. */
        XRRChangeOutputProperty(dpy, output,
                                MCompAtoms::randr.graphics_alpha,
                                XA_INTEGER, 32, PropModeReplace,
                                (unsigned char *)&new_gralpha, 1);
        gralpha = new_gralpha;
    }
    if (new_vidalpha >= 0 && new_vidalpha != vidalpha) {
        /* Change or reset video alpha. */
        XRRChangeOutputProperty(dpy, output,
                                MCompAtoms::randr.video_alpha,
                                XA_INTEGER, 32, PropModeReplace,
                                (unsigned char *)&new_vidalpha, 1);
        vidalpha = new_vidalpha;
    }

    if (blend != blending && blend)
        /* Enable blending last. */
        XRRChangeOutputProperty(dpy, output,
                                MCompAtoms::randr.alpha_mode,
                                XA_INTEGER, 32, PropModeReplace,
                                (unsigned char *)&blend, 1);
    blending = blend;
}

/* Turn off global alpha blending on both planes. */
static void reset_global_alpha()
{
    set_global_alpha(255, 255);
}

static Bool map_predicate(Display *display, XEvent *xevent, XPointer arg)
{
    Q_UNUSED(display);
    Window window = (Window)arg;
    if (xevent->type == MapNotify && xevent->xmap.window == window)
        return True;
    return False;
}

static void kill_window(MCompositeWindow *window)
{
    int pid = window->propertyCache()->pid();
    if (pid == getpid())
        return;
    if (pid != 0) {
        // negative PID to kill the whole process group
        ::kill(-pid, SIGKILL);
        ::kill(pid, SIGKILL);
    }
    if (window->isValid() && window->type() != MSplashScreen::Type)
        XKillClient(QX11Info::display(), window->window());
}

static void safe_move(QList<Window>& winlist, int from, int to)
{
    if (from == to)
        return;
    int slsize = winlist.size();
    if ((0 <= from && from < slsize) && (0 <= to && to < slsize))
        winlist.move(from,to);
    else
        qWarning("safe_move(%d -> %d); nwins=%d", from, to, slsize);
}

MCompositeManagerPrivate::MCompositeManagerPrivate(MCompositeManager *p)
    : QObject(p),
      prev_focus(0),
      buttoned_win(0),
      glwidget(0),
      desktop_window(0),
      compositing(true),
      changed_properties(false),
      orientationProvider(p->configInt("default-desktop-angle")),
      prepared(false),
      stacking_timeout_check_visibility(false),
      stacking_timeout_timestamp(CurrentTime),
      splash(0),
      lastDestroyedSplash(0, 0)
{
    xcb_conn = XGetXCBConnection(QX11Info::display());
    MWindowPropertyCache::set_xcb_connection(xcb_conn);
    xserver_stacking.init(QX11Info::display());

    watch = new MCompositeScene(this);
    MCompAtoms::init();

    device_state = new MDeviceState(this);
    watch->keep_black = device_state->displayOff();
    connect(device_state, SIGNAL(displayStateChange(bool)),
            this, SLOT(displayOff(bool)));
    connect(device_state, SIGNAL(callStateChange(bool)),
            this, SLOT(callOngoing(bool)));
    stacking_timer.setSingleShot(true);
    connect(&stacking_timer, SIGNAL(timeout()), this, SLOT(stackingTimeout()));
    lockscreen_map_timer.setSingleShot(true);
    lockscreen_map_timer.setInterval(qobject_cast<MCompositeManager*>(p)->
                                     configInt("lockscreen-map-timeout-ms"));
    connect(&lockscreen_map_timer, SIGNAL(timeout()), p,
            SLOT(lockScreenPainted()));
    connect(this, SIGNAL(currentAppChanged(Window)), this,
            SLOT(setupButtonWindows(Window)));
}

MCompositeManagerPrivate::~MCompositeManagerPrivate()
{
    if (prepared && localwin != None)
        // Advertise the world we're gone.
        XDeleteProperty(QX11Info::display(), QX11Info::appRootWindow(),
                        ATOM(_NET_SUPPORTING_WM_CHECK));

    delete watch;
    watch = 0;
}

#ifdef WINDOW_DEBUG
void MCompositeManagerPrivate::setWindowDebugProperties(Window w)
{
    if (!debug_mode) return;
    MCompositeWindow *i = COMPOSITE_WINDOW(w);
    if (!i)
        return;

    CARD32 d[1];
    if (i->isVisible())
        d[0] = i->isDirectRendered() ? ATOM(_M_WM_WINDOW_DIRECT_VISIBLE)
                                     : ATOM(_M_WM_WINDOW_COMPOSITED_VISIBLE);
    else
        d[0] = i->isDirectRendered() ? ATOM(_M_WM_WINDOW_DIRECT_INVISIBLE)
                                     : ATOM(_M_WM_WINDOW_COMPOSITED_INVISIBLE);

    XChangeProperty(QX11Info::display(), w, ATOM(_M_WM_INFO), XA_ATOM,
                    32, PropModeReplace, (unsigned char *)d, 1);
    long z = i->zValue();
    XChangeProperty(QX11Info::display(), w, ATOM(_M_WM_WINDOW_ZVALUE),
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *) &z, 1);
}
#else
#define setWindowDebugProperties(X)
#endif

static void setup_key_grabs()
{
    Display* dpy = QX11Info::display();
    static bool ignored_mod = false;
    if (!switcher_key) {
        QString k = static_cast<MCompositeManager*>(qApp)->config(
                                          "switcher-keysym").toString();
        switcher_key = XKeysymToKeycode(dpy,
                             XStringToKeysym(k.toLatin1().constData()));
        XGrabKey(dpy, switcher_key, Mod5Mask,
                 RootWindow(QX11Info::display(), 0), True,
                 GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, switcher_key, Mod5Mask | LockMask,
                 RootWindow(QX11Info::display(), 0), True,
                 GrabModeAsync, GrabModeAsync);
    }

    if (!ignored_mod) {
        XkbDescPtr xkb_t;

        if ((xkb_t = XkbAllocKeyboard()) == NULL)
            return;

        if (XkbGetControls(dpy, XkbAllControlsMask, xkb_t) == Success)
            XkbSetIgnoreLockMods(dpy, xkb_t->device_spec, Mod5Mask, Mod5Mask,
                                 0, 0);
        XkbFreeControls(xkb_t, 0, True);
        ignored_mod = true;
        free(xkb_t);
    }
}

void MCompositeManagerPrivate::prepare()
{
    MDecoratorFrame::instance();
    const char *wm_name = "MCompositor";

    wm_window = XCreateSimpleWindow(QX11Info::display(),
                            RootWindow(QX11Info::display(), 0),
                            0, 0, 1, 1, 0,
                            None, None);
    if (localwin != None)
        XChangeProperty(QX11Info::display(), RootWindow(QX11Info::display(), 0),
                        ATOM(_NET_SUPPORTING_WM_CHECK), XA_WINDOW, 32,
                        PropModeReplace, (unsigned char *)&wm_window, 1);
    XChangeProperty(QX11Info::display(), wm_window,
                    ATOM(_NET_SUPPORTING_WM_CHECK),
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&wm_window, 1);
    XChangeProperty(QX11Info::display(), wm_window, ATOM(_NET_WM_NAME),
                    XInternAtom(QX11Info::display(), "UTF8_STRING", 0), 8,
                    PropModeReplace, (unsigned char *) wm_name,
                    strlen(wm_name));
    setup_key_grabs();

    Xutf8SetWMProperties(QX11Info::display(), wm_window, "MCompositor",
                         "MCompositor", NULL, 0, NULL, NULL,
                         NULL);
    Atom a = XInternAtom(QX11Info::display(), "_NET_WM_CM_S0", False);
    XSetSelectionOwner(QX11Info::display(), a, wm_window, 0);
    XSelectInput(QX11Info::display(), wm_window, PropertyChangeMask);

    xoverlay = XCompositeGetOverlayWindow(QX11Info::display(),
                                          RootWindow(QX11Info::display(), 0));
    overlay_mapped = false; // make sure we try to map it in startup
    if (localwin)
        XReparentWindow(QX11Info::display(), localwin, xoverlay, 0, 0);
    localwin_parent = xoverlay;

    XDamageQueryExtension(QX11Info::display(), &damage_event, &damage_error);

    // create InputOnly windows for close and Home button handling
    close_button_win = XCreateWindow(QX11Info::display(),
                                     RootWindow(QX11Info::display(), 0),
                                     -1, -1, 1, 1, 0, CopyFromParent,
                                     InputOnly, CopyFromParent, 0, 0);
    XStoreName(QX11Info::display(), close_button_win, "MCompositor close button");
    XSelectInput(QX11Info::display(), close_button_win,
                 ButtonReleaseMask | ButtonPressMask);
    XMapWindow(QX11Info::display(), close_button_win);
    home_button_win = XCreateWindow(QX11Info::display(),
                                    RootWindow(QX11Info::display(), 0),
                                    -1, -1, 1, 1, 0, CopyFromParent,
                                    InputOnly, CopyFromParent, 0, 0);
    XStoreName(QX11Info::display(), home_button_win, "MCompositor home button");
    XSelectInput(QX11Info::display(), home_button_win,
                 ButtonReleaseMask | ButtonPressMask);
    XMapWindow(QX11Info::display(), home_button_win);

    prepared = true;
}

void MCompositeManagerPrivate::loadPlugin(const QString &fileName)
{
        QObject *plugin;
        MCompmgrExtensionFactory* factory;
        QPluginLoader loader(fileName);
        if (!(plugin = loader.instance()))
            qFatal("couldn't load %s: %s",
                   fileName.toLatin1().constData(),
                   loader.errorString().toLatin1().constData());
        if (!(factory = qobject_cast<MCompmgrExtensionFactory *>(plugin)))
            qFatal("%s is not a MCompmgrExtensionFactory",
                   fileName.toLatin1().constData());
        factory->create();
}

int MCompositeManagerPrivate::loadPlugins(const QDir &dir)
{
    int nloaded = 0;
    foreach (QString fileName, dir.entryList(QDir::Files)) {
        if (!QLibrary::isLibrary(fileName)) {
            qWarning() << fileName << "doesn't look like a library, skipping";
            continue;
        }
        loadPlugin(dir.absoluteFilePath(fileName));
        nloaded++;
    }
    return nloaded;
}

bool MCompositeManagerPrivate::needDecoration(MWindowPropertyCache *pc)
{
    Q_ASSERT(pc);
    if (pc->isInputOnly() || pc->isDecorator() || pc->isOverrideRedirect())
        return false;
    if (DECORATED_FS_WINDOW(pc))
        // fullscreen window is decorated during call
        return true;
    if (FULLSCREEN_WINDOW(pc))
        return false;
    MCompAtoms::Type t = pc->windowType();
    return (t != MCompAtoms::FRAMELESS
            && t != MCompAtoms::DESKTOP
            && t != MCompAtoms::NOTIFICATION
            && t != MCompAtoms::INPUT
            && t != MCompAtoms::DOCK
            && t != MCompAtoms::NO_DECOR_DIALOG
            && t != MCompAtoms::SHEET
            && (!getLastVisibleParent(pc) || t == MCompAtoms::DIALOG));
}

void MCompositeManagerPrivate::damageEvent(XDamageNotifyEvent *e)
{
    MCompositeWindow *item = COMPOSITE_WINDOW(e->drawable);
    if (item) {
        item->propertyCache()->damageReceived();
        /* partial updates disabled for now, does not always work, unless we
         * check for EGL_BUFFER_PRESERVED or GLX_SWAP_COPY_OML first, see
         * http://www.khronos.org/registry/egl/specs/EGLTechNote0001.html and
         * http://www.opengl.org/registry/specs/OML/glx_swap_method.txt */
        if ((item->isVisible() || !item->paintedAfterMapping())
            && !device_state->displayOff())
            item->updateWindowPixmap(0, 0, e->timestamp);
        item->damageReceived();
    }
}

void MCompositeManagerPrivate::destroyEvent(XDestroyWindowEvent *e)
{
    MCompositeWindow *item = COMPOSITE_WINDOW(e->window);
    if (item) {
        item->deleteLater();
        removeWindow(item->window());
        prop_caches.remove(item->window());
        // PC deleted with the MCompositeWindow.  Till then we've made sure
        // that we can't reuse it, even if a window with the same XID is
        // created before @item is actually destroyed.
    } else {
#ifdef ENABLE_BROKEN_SIMPLEWINDOWFRAME
        // We got a destroy event from a framed window (or a window that was
        // never mapped)
        FrameData fd = framed_windows.value(e->window);
        if (fd.frame) {
            framed_windows.remove(e->window);
            delete fd.frame;
        }
#endif // ENABLE_BROKEN_SIMPLEWINDOWFRAME
        if (prop_caches.contains(e->window)) {
            delete prop_caches.value(e->window);
            prop_caches.remove(e->window);
        }
        removeWindow(e->window);
    }
}

void MCompositeManagerPrivate::splashTimeout()
{
    if (!splash)
        goto check_compositing_and_stacking;

    lastDestroyedSplash = DestroyedSplash(splash->window(),
                                          splash->propertyCache()->pid());

    splash->hide();
    prop_caches.remove(splash->window());
    removeWindow(splash->window());
    splash->deleteLater();
    splash = 0;
    waiting_damage = 0;
check_compositing_and_stacking:
    glwidget->update();
    dirtyStacking(false);

    if (MWindowPropertyCache *pc = prop_caches.value(current_app, 0))
        orientationProvider.updateCurrentWindowOrienationAngle(pc);
}

void MCompositeManagerPrivate::propertyEvent(XPropertyEvent *e)
{
    MWindowPropertyCache *pc;

    if (e->atom == ATOM(_MEEGO_SPLASH_SCREEN) && e->window == wm_window
        && e->state == PropertyNewValue) {
        unsigned pid, pixmap;
        QString lscape, portrait;

        if (device_state->touchScreenLock() == "locked")
            return; // :-P
        if (MWindowPropertyCache::readSplashProperty(wm_window,
                                                    pid, pixmap,
                                                    lscape, portrait)) {
            if (splash) {
                // Are we seeing a duplicate PropertyNewValue as a result
                // of somebody building the property value gradually?
                if (splash->same(pid, portrait, lscape, pixmap))
                    return;
                splashTimeout();
            }
            splash = new MSplashScreen(pid, portrait, lscape, pixmap);
            lastDestroyedSplash = DestroyedSplash(0, 0);
            if (!splash->windowPixmap()) {
                // no pixmap and/or failed to load the image file
                splash->deleteLater();
                splash = 0;
                return;
            }
            windows[splash->window()] = splash;
            prop_caches[splash->window()] = splash->propertyCache();
            setWindowState(splash->window(), NormalState);
            STACKING("adding 0x%lx to stack", splash->window());
            stacking_list.append(splash->window());
            roughSort();
            splash->setZValue(stacking_list.indexOf(splash->window()));
            splash->setNewlyMapped(false);
            addItem(splash);
            enableCompositing();
            emit windowBound(splash);
            splash->showWindow();
            dirtyStacking(false);
            orientationProvider.updateCurrentWindowOrienationAngle(splash->propertyCache());

            // remove entries older than one minute
            QHash<unsigned, DismissedSplash>::iterator it = dismissedSplashScreens.begin();
            while (it != dismissedSplashScreens.end()) {
                DismissedSplash &ds = it.value();
                if (ds.lifetimeTimer.elapsed() > 60 * 1000
                        || (ds.blockTimer.isValid()
                        && ds.blockTimer.elapsed() > 1000))
                    it = dismissedSplashScreens.erase(it);
                else
                    ++it;
            }
        }
        return;
    }

    if (!prop_caches.contains(e->window))
        return;
    pc = prop_caches.value(e->window);

    if (e->atom == ATOM(_MEEGO_LOW_POWER_MODE)) {
        pc->propertyEvent(e);
        dirtyStacking(true, e->time); // visibility notify
        // check if compositing needs to be switched on/off
        if (pc->isMapped() &&
            !possiblyUnredirectTopmostWindow() && !compositing)
            enableCompositing();
        return;
    }

    if (pc->propertyEvent(e) && pc->isMapped()) {
        bool recheck_visibility = false;
        changed_properties = true; // property change can affect stacking order
        if (pc->isDecorator())
            // in case decorator's transiency changes, make us update the value
            pc->transientFor();
        // if transient, ensure that it has the same state as the parent
        Window p;
        if (e->atom == ATOM(WM_TRANSIENT_FOR) &&
            (p = getLastVisibleParent(pc))) {
            MWindowPropertyCache *p_pc = prop_caches.value(p, 0);
            if (p_pc) setWindowState(e->window, p_pc->windowState());
        }
        if (e->atom == ATOM(_MEEGOTOUCH_OPAQUE_WINDOW))
            recheck_visibility = true;
        MCompositeWindow *cw = COMPOSITE_WINDOW(e->window);
        if (cw)
            cw->setDecorated(needDecoration(pc));
        // handle stacking on the animator for single (non-chained) window
        bool restoring = (cw && 
                          cw->propertyCache()->invokedBy() == None &&
                          cw->status() == MCompositeWindow::Restoring);
        if (!restoring)
            dirtyStacking(recheck_visibility, e->time);
        if (cw && !cw->isNewlyMapped() && !restoring) {
            checkStacking(recheck_visibility, e->time);
            // window on top could have changed
            if (!possiblyUnredirectTopmostWindow() && !compositing)
                enableCompositing();
        }
    }

    // global alpha events here. TODO: property cache class could handle this
    // but it is straightforward to manipulate it from here
    if (pc->isMapped() && (e->atom == ATOM(_MEEGOTOUCH_GLOBAL_ALPHA) ||
        e->atom == ATOM(_MEEGOTOUCH_VIDEO_ALPHA)))
        dirtyStacking(true, e->time);

    if (pc->isMapped() && e->atom == ATOM(_MEEGOTOUCH_ORIENTATION_ANGLE)) {
        if (e->window == desktop_window)
            orientationProvider.updateDesktopOrientationAngle(pc);
        if (e->window == current_app) {
            orientationProvider.updateCurrentWindowOrienationAngle(pc);
            // reset managed window to apply the new orientation
            MDecoratorFrame *deco = MDecoratorFrame::instance();
            MCompositeWindow *cw = COMPOSITE_WINDOW(e->window);
            if (deco->managedWindow() == e->window && cw) {
                if (cw->status() == MCompositeWindow::Hung)
                    deco->setManagedWindow(cw, true);
                else if (DECORATED_FS_WINDOW(cw->propertyCache()))
                    deco->setManagedWindow(cw, true, true);
                else
                    deco->setManagedWindow(cw);
            }
        }
    }
}

Window MCompositeManagerPrivate::getLastVisibleParent(MWindowPropertyCache *pc)
{
    Window last = 0, parent;
    MWindowPropertyCache *orig_pc = pc;
    while (pc && (parent = pc->transientFor())) {
       pc = prop_caches.value(parent, 0);
       if (pc == orig_pc) {
           qWarning("%s(): window 0x%lx belongs to a transiency loop!",
                    __func__, orig_pc->winId());
           return 0;
       }
       if (pc && pc->isMapped())
           last = parent;
       else // no-good parent, bail out
           break;
    }
    return last;
}

Window MCompositeManager::getLastVisibleParent(MWindowPropertyCache *pc) const
{
    return d->getLastVisibleParent(pc);
}

Window MCompositeManagerPrivate::getTopmostApp(int *index_in_stacking_list,
                                               Window ignore_window,
                                               bool skip_always_mapped)
{
    GTA("ignore 0x%lx, skip_always_mapped: %d",
        ignore_window, skip_always_mapped);

    for (int i = stacking_list.size() - 1; i >= 0; --i) {
        Window w = stacking_list.at(i);
        GTA("considering 0x%lx", w);
        if (w == ignore_window || !w) {
            GTA("ignoring");
            continue;
        }
        if (w == desktop_window) {
            /* desktop is above all applications */
            GTA("  desktop layer reached");
            return 0;
        }

        MCompositeWindow *cw = COMPOSITE_WINDOW(w);
        MWindowPropertyCache *pc;
        if (!cw) {
            GTA("  has no MCompositeWindow");
            continue;
        } else if (!cw->isMapped()) {
            GTA("  not mapped");
            continue;
        } else if (!(pc = cw->propertyCache())) {
            GTA("  has no property cache");
            continue;
        } else if (skip_always_mapped && pc->alwaysMapped()) {
            GTA("  has _MEEGOTOUCH_ALWAYS_MAPPED");
            continue;
        }
        // NOTE: this WILL pass transient application window and non-transient
        // menu (this is intended!)
        if ((pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_MENU)
             && getLastVisibleParent(pc))
            || (pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_MENU)
                && !cw->isAppWindow(true))) {
            GTA("  not an application window (or non-transient menu)");
            continue;
        }
        if (pc->windowState() != NormalState) {
            GTA("  not in normal state");
            continue;
        } else if (cw->isWindowTransitioning() &&
                   !cw->isNotChangingStacking()) {
            GTA("  is transitioning");
            continue;
        }

        GTA("  suitable");
        if (index_in_stacking_list)
            *index_in_stacking_list = i;
        return w;
    }
    GTA("no suitable window found");
    return 0;
}

MCompositeWindow *MCompositeManagerPrivate::getHighestDecorated(int *index)
{
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
        Window w = stacking_list.at(i);
        if (w == desktop_window)
            break;
        MCompositeWindow *cw = COMPOSITE_WINDOW(w);
        MWindowPropertyCache *pc;
        if (cw && cw->isMapped() && (pc = cw->propertyCache()) &&
            pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_INPUT) &&
            !pc->isOverrideRedirect() &&
            (cw->needDecoration() || cw->status() == MCompositeWindow::Hung
             || DECORATED_FS_WINDOW(pc))) {
            if (index) *index = i;
            return cw;
        }
    }
    if (index) *index = -1;
    return 0;
}

bool MCompositeManagerPrivate::haveMappedWindow() const
{
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
        Window w = stacking_list.at(i);
        MWindowPropertyCache *pc = prop_caches.value(w, 0);
        if (pc && pc->is_valid && pc->isMapped())
            return true;
    }
    return false;
}

bool MCompositeManagerPrivate::possiblyUnredirectTopmostWindow()
{
    if (watch->keep_black || (splash && !device_state->displayOff()))
        return false;
    static const QRegion fs_r(0, 0,
                    ScreenOfDisplay(QX11Info::display(),
                        DefaultScreen(QX11Info::display()))->width,
                    ScreenOfDisplay(QX11Info::display(),
                        DefaultScreen(QX11Info::display()))->height);
    bool ret = false;
    Window top = 0;
    int win_i = -1;
    MCompositeWindow *cw = 0;
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
        Window w = stacking_list.at(i);
        if (!(cw = COMPOSITE_WINDOW(w)) || cw->propertyCache()->isInputOnly()
                || (!splash && cw->type() == MSplashScreen::Type))
            continue;
        if (w == desktop_window) {
            top = w;
            win_i = i;
            break;
        }
        if (cw->isClosing())
            // this window is unmapped and has unmap animation going on
            return false;
        Window parent;
        MCompositeWindow *p_cw;
        if (cw->propertyCache()->windowTypeAtom() ==
                                       ATOM(_NET_WM_WINDOW_TYPE_INPUT)
            && (parent = cw->propertyCache()->transientFor())
            && (p_cw = COMPOSITE_WINDOW(parent))
            && (p_cw->isClosing() || p_cw->isWindowTransitioning()))
            // input method window's transient parent is animating
            return false;
        if (!cw->paintedAfterMapping())
            return false;
        if (cw->isMapped() && (cw->needsCompositing()
            // FIXME: implement direct rendering for shaped windows
            || !fs_r.subtracted(cw->propertyCache()->shapeRegion()).isEmpty()))
            // this window prevents direct rendering
            return false;
        // it is a fullscreen, non-transparent window of any type
        if (cw->isMapped()) {
            top = w;
            win_i = i;
            break;
        }
    }

    // this code prevents us disabling compositing when we have a window
    // that has XMapWindow() called but we have not yet received the MapNotify
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
        Window w = stacking_list.at(i);
        if (w == desktop_window) break;
        MWindowPropertyCache *pc = prop_caches.value(w, 0);
        if (pc && pc->is_valid && pc->beingMapped())
            return false;
    }
    if (!haveMappedWindow()) {
        if (splash || device_state->displayOff()
            || MCompositeWindow::hasTransitioningWindow())
            return false;
        showOverlayWindow(false);
        compositing = false;
        return true;
    }

    if (top && cw && !MCompositeWindow::hasTransitioningWindow()) {
#ifdef GLES2_VERSION
        if (compositing) {
            showOverlayWindow(false);
            compositing = false;
        }
#endif
        // allow input method window to composite its client window
        Window parent;
        MCompositeWindow *p_cw;
        if (cw->propertyCache()->windowTypeAtom() ==
                                       ATOM(_NET_WM_WINDOW_TYPE_INPUT) &&
            (parent = cw->propertyCache()->transientFor()) &&
            (p_cw = COMPOSITE_WINDOW(parent)) && p_cw->isMapped())
            if (((MTexturePixmapItem*)p_cw)->isDirectRendered()) {
                ((MTexturePixmapItem*)p_cw)->enableRedirectedRendering();
                setWindowDebugProperties(parent);
            }
        // unredirect the chosen window and any docks and OR windows above it
        // TODO: what else should be unredirected?
        if (!((MTexturePixmapItem *)cw)->isDirectRendered()) {
            ((MTexturePixmapItem *)cw)->enableDirectFbRendering();
            setWindowDebugProperties(top);
        }
        for (int i = win_i + 1; i < stacking_list.size(); ++i) {
            Window w = stacking_list.at(i);
            if ((cw = COMPOSITE_WINDOW(w)) && cw->isMapped() &&
                (cw->propertyCache()->windowTypeAtom()
                                   == ATOM(_NET_WM_WINDOW_TYPE_DOCK)
                 || cw->propertyCache()->isOverrideRedirect())) {
                if (!((MTexturePixmapItem *)cw)->isDirectRendered()) {
                    ((MTexturePixmapItem *)cw)->enableDirectFbRendering();
                    setWindowDebugProperties(w);
                }
            }
        }
#ifndef GLES2_VERSION
        if (compositing) {
            showOverlayWindow(false);
            compositing = false;
        }
#endif
        ret = true;
    }
    return ret;
}

void MCompositeManagerPrivate::unmapEvent(XUnmapEvent *e)
{
    // if desktop window was unmapped we need to set the appropriate context
    // property to default value
    if (e->window == desktop_window)
        orientationProvider.updateDesktopOrientationAngle(0);

    if (e->send_event == True || e->event != QX11Info::appRootWindow())
        // handle root's SubstructureNotifys (top-levels) only
        return;
    MWindowPropertyCache *wpc = 0;
    if (prop_caches.contains(e->window)) {
        wpc = prop_caches.value(e->window);
        wpc->setBeingMapped(false);
        wpc->setIsMapped(false);
        wpc->setStackedUnmapped(false);
        if (!wpc->isInputOnly()
            && wpc->parentWindow() != QX11Info::appRootWindow())
            XRemoveFromSaveSet(QX11Info::display(), e->window);
        if (wpc->isDecorator())
            MDecoratorFrame::instance()->setDecoratorItem(0);
        if (wpc->isLockScreen())
            lockscreen_painted = false;
    }

    // do not keep unmapped windows in windows_as_mapped list
    updateNetClientList(e->window, false);

    if (e->window == xoverlay) {
        overlay_mapped = false;
        return;
    }

    MCompositeWindow *item = COMPOSITE_WINDOW(e->window);
    if (item) {
        item->stopCloseTimer();
        if (!wpc->noAnimations())
            item->closeWindowAnimation();
        // mark obscured so that we send unobscured if it's remapped
        item->setWindowObscured(true, true);
        item->stopPing();
        item->setIsMapped(false);
        setWindowState(e->window, WithdrawnState);
        if (item->isVisible() && !item->isClosing())
            item->setVisible(false);

        MDecoratorFrame *deco = MDecoratorFrame::instance();
        if (deco->managedWindow() == e->window)
            dirtyStacking(false); // reset decorator's managed window
    } else {
#ifdef ENABLE_BROKEN_SIMPLEWINDOWFRAME
        // We got an unmap event from a framed window
        FrameData fd = framed_windows.value(e->window);
        if (!fd.frame)
            return;
        // make sure we reparent first before deleting the window
        XGrabServer(QX11Info::display());
        XReparentWindow(QX11Info::display(), e->window,
                        RootWindow(QX11Info::display(), 0), 0, 0);
        setWindowState(e->window, IconicState);
        framed_windows.remove(e->window);
        XUngrabServer(QX11Info::display());
        delete fd.frame;
#else // ! ENABLE_BROKEN_SIMPLEWINDOWFRAME
        return;
#endif
    }

    if (e->window == desktop_window)
        desktop_window = 0;

    // Force visibility check because the unmapped window may not have been
    // in the prev_stacked_mapped list of checkStacking(), causing skipping
    // the visibility calculation when this window is unmapped.
    dirtyStacking(true);
}

void MCompositeManagerPrivate::configureEvent(XConfigureEvent *e,
                                              bool nostacking)
{
    if (e->window == xoverlay || e->window == localwin
        || e->window == close_button_win || e->window == home_button_win)
        return;

    MWindowPropertyCache *pc = prop_caches.value(e->window, 0);
    if (!pc || !pc->is_valid)
        return;

    bool check_visibility = false;
    MCompositeWindow *item = COMPOSITE_WINDOW(e->window);
    QRect &exp = pc->expectedGeometry();
    if (exp == QRect(e->x, e->y, e->width, e->height))
        exp = QRect();

    if (exp.isEmpty()) {
        QRect g = pc->realGeometry();
        if (e->x != g.x() || e->y != g.y() || e->width != g.width() ||
            e->height != g.height()) {
            QRect r(e->x, e->y, e->width, e->height);
            pc->setRealGeometry(r);
            if (pc->isMapped())
                check_visibility = true;
        }
        if (item && pc->windowState() != IconicState) {
            if (item->type() != MSplashScreen::Type &&
                (check_visibility || !item->isWindowTransitioning()))
                item->setPos(e->x, e->y);
            item->resize(e->width, e->height);
        }
    }

    // Don't mess with stacking if it's just MDecoratorFrame wanting
    // to let us know the position of the managed window in advance.
    if (!nostacking) {
        if (check_visibility ||
            // restack & reset decorator's managed window
            (item && pc->isMapped() && item->needDecoration()))
            dirtyStacking(check_visibility);
    } else if (check_visibility)
        // this is a must because when the real ConfigureEvent comes,
        // there is no change in realGeometry() so check_visibility is false
        sendSyntheticVisibilityEventsForOurBabies();
}

// used to handle ConfigureRequest when we have the object for the window
void MCompositeManagerPrivate::configureWindow(MWindowPropertyCache *pc,
                                               XConfigureRequestEvent *e)
{
    if (e->value_mask & (CWX | CWY | CWWidth | CWHeight)) {
        if (FULLSCREEN_WINDOW(pc))
            // do not allow resizing of fullscreen window
            e->value_mask &= ~(CWX | CWY | CWWidth | CWHeight);
        else {
            QRect r = pc->requestedGeometry();
            if (e->value_mask & CWX)
                r.setX(e->x);
            if (e->value_mask & CWY)
                r.setY(e->y);
            if (e->value_mask & CWWidth)
                r.setWidth(e->width);
            if (e->value_mask & CWHeight)
                r.setHeight(e->height);
            pc->setRequestedGeometry(r);
        }
    }

    /* modify stacking_list if stacking order should be changed */
    int win_i = stacking_list.indexOf(e->window);
    if (win_i >= 0 && e->detail == Above && (e->value_mask & CWStackMode)) {
        if (e->value_mask & CWSibling) {
            int above_i = stacking_list.indexOf(e->above);
            if (above_i >= 0) {
                Window d = desktop_window;
                if (d && pc->isMapped() && stacking_list.indexOf(d) > above_i)
                    // mark iconic if it goes under desktop
                    setWindowState(e->window, IconicState);
                if (above_i > win_i) {
                    STACKING_MOVE(win_i, above_i);
                    safe_move(stacking_list, win_i, above_i);
                } else {
                    STACKING_MOVE(win_i, above_i+1);
                    safe_move(stacking_list, win_i, above_i + 1);
                }
                dirtyStacking(false);
            }
        } else {
            Window parent = pc->transientFor();
            if (parent) {
                STACKING("positionWindow 0x%lx -> top", parent);
                positionWindow(parent, true);
            } else {
                MCompositeWindow* cw = COMPOSITE_WINDOW(e->window);
                // For restoring windows, they are positioned on top only
                // ONCE the animation has begun so it doesn't flicker
                // briefly while animating in.
                // Handle stacking on the animator for single (non-chained) 
                // window
                bool restoring = (cw && 
                                  cw->propertyCache()->invokedBy() == None &&
                                  cw->status() == MCompositeWindow::Restoring);
                bool iconifiedOnPurpose = false;
                if (cw) {
                    QHash<unsigned int, DismissedSplash>::iterator it =
                        dismissedSplashScreens.find(cw->propertyCache()->pid());
                    if (it != dismissedSplashScreens.end()) {
                        iconifiedOnPurpose = true;
                        if (!it.value().blockTimer.isValid())
                            it.value().blockTimer.start();
                    }
                }
                if (!restoring && !iconifiedOnPurpose) {
                    STACKING("positionWindow 0x%lx -> top",e-> window);
                    positionWindow(e->window, true);
                } else
                    return; // dont honor raise
            }
        }
    } else if (win_i >= 0 && e->detail == Below
               && (e->value_mask & CWStackMode)) {
        if (e->value_mask & CWSibling) {
            int above_i = stacking_list.indexOf(e->above);
            if (above_i >= 0) {
                Window d = desktop_window;
                if (d && pc->isMapped() && stacking_list.indexOf(d) >= above_i)
                    // mark iconic if it goes under desktop
                    setWindowState(e->window, IconicState);
                if (above_i > win_i) {
                    STACKING_MOVE(win_i, above_i-1);
                    safe_move(stacking_list, win_i, above_i - 1);
                } else {
                    STACKING_MOVE(win_i, above_i);
                    safe_move(stacking_list, win_i, above_i);
                }
                dirtyStacking(false);
            }
        } else {
            Window parent = pc->transientFor();
            if (parent && pc->isMapped()) {
                // lower the parent only if the window is mapped
                MWindowPropertyCache *ppc = prop_caches.value(parent, 0);
                if (ppc && ppc->isMapped())
                    setWindowState(parent, IconicState);
                STACKING("positionWindow 0x%lx -> bottom", parent);
                positionWindow(parent, false);
            } else {
                if (pc->isMapped())
                    setWindowState(e->window, IconicState);
                STACKING("positionWindow 0x%lx -> bottom", e->window);
                positionWindow(e->window, false);
            }
        }
    }
    if ((e->value_mask & CWStackMode) && !pc->isMapped())
        // prevent unnecessary stacking order changes at mapping time
        pc->setStackedUnmapped(true);

    /* Resize and/or reposition the X window if requested. Stacking changes
     * were done above. */
    unsigned int value_mask = e->value_mask & ~(CWSibling | CWStackMode);
    if (value_mask) {
        XWindowChanges wc;
        wc.border_width = e->border_width;
        wc.x = e->x;
        wc.y = e->y;
        wc.width = e->width;
        wc.height = e->height;
        wc.sibling =  e->above;
        wc.stack_mode = e->detail;
        XConfigureWindow(QX11Info::display(), e->window, value_mask, &wc);
        RECONFIG(e->window, value_mask, e->x, e->y, wc.width, wc.height);
    }
}

MWindowPropertyCache *MCompositeManagerPrivate::getPropertyCache(Window w,
                                xcb_get_window_attributes_reply_t *attrs,
                                xcb_get_geometry_reply_t *geom,
                                Damage damage_obj)
{
    MWindowPropertyCache *pc = prop_caches.value(w, 0);
    if (pc) return pc;
    pc = new MWindowPropertyCache(w, attrs, geom, damage_obj);
    if (!pc->is_valid) {
        delete pc;
        return 0;
    }
    prop_caches[w] = pc;
    if (!stacking_list.contains(w)) {
        // add it to stacking_list to allow configures before mapping
        STACKING("adding 0x%lx to stack", w);
        stacking_list.append(w);
    }
    return pc;
}

void MCompositeManagerPrivate::configureRequestEvent(XConfigureRequestEvent *e)
{
    if (e->parent != RootWindow(QX11Info::display(), 0))
        return;

    MWindowPropertyCache *pc = getPropertyCache(e->window);

    // sandbox these windows. we own them
    if (!pc || !pc->is_valid || pc->isDecorator())
        return;

    /*qDebug() << __func__ << "CONFIGURE REQUEST FOR:" << e->window
        << e->x << e->y << e->width << e->height << "above/mode:"
        << e->above << e->detail << (e->value_mask & CWStackMode);*/

    configureWindow(pc, e);
    MCompositeWindow *i = COMPOSITE_WINDOW(e->window);
    if (!i || !pc->isMapped() || dismissedSplashScreens.contains(pc->pid()))
        return;

    MCompAtoms::Type wtype = i->propertyCache()->windowType();
    if (e->detail == Above && e->above == None && wtype != MCompAtoms::DESKTOP
        && wtype != MCompAtoms::INPUT && wtype != MCompAtoms::DOCK) {
        setWindowState(e->window, NormalState);
        i->setIconified(false);

        // redirect home to keep it prepared for animations (if it's redirected
        // just before the animation, it does not always draw in time)
        MCompositeWindow *d = COMPOSITE_WINDOW(desktop_window);
        if (d && ((MTexturePixmapItem *)d)->isDirectRendered()) {
            ((MTexturePixmapItem *)d)->enableRedirectedRendering();
            setWindowDebugProperties(d->window());
        }
        setExposeDesktop(false);

        if (MDecoratorFrame::instance()->managedWindow() == e->window)
            enableCompositing();
    }
}

void MCompositeManagerPrivate::mapRequestEvent(XMapRequestEvent *e)
{
    Display *dpy = QX11Info::display();
    Damage damage_obj = 0;
    // create the damage object before mapping to get 'em all
    MWindowPropertyCache *pc = prop_caches.value(e->window, 0);
    if (!device_state->displayOff()) {
        if (pc) {
            // allow input method window to composite its client window
            Window parent;
            bool unredir = false;
            MCompositeWindow *p_cw;
            if (pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_INPUT)
                && (parent = pc->transientFor()) &&
                (p_cw = COMPOSITE_WINDOW(parent)) && p_cw->isMapped()
                && getTopmostApp() == parent) {
                // redirect the parent
                if (((MTexturePixmapItem*)p_cw)->isDirectRendered()) {
                    ((MTexturePixmapItem*)p_cw)->enableRedirectedRendering();
                    setWindowDebugProperties(parent);
                }

                // unredirect the input method window if possible
                unredir = !pc->hasAlphaAndIsNotOpaque();
                if (compositing) {
                    // It's not if we're showing any hasAlphaAndIsNotOpaque()
                    // windows, for example notifications.  If we did unredir,
                    // those windows would disappear until we realize our
                    // mistake and undo.
                    QHash<Window, MCompositeWindow*>::const_iterator it;
                    for (it = windows.begin();
                         unredir && it != windows.end(); ++it)
                        unredir = !it.value()->isVisible() || !it.value()->propertyCache()->hasAlphaAndIsNotOpaque();
                }
            }
            if (unredir) {
                if (compositing) {
                    showOverlayWindow(false);
                    compositing = false;
                }
                MCompositeWindow *cw = COMPOSITE_WINDOW(e->window);
                if (cw) {
                    ((MTexturePixmapItem*)cw)->enableDirectFbRendering();
                    setWindowDebugProperties(e->window);
                } else {
                    XCompositeUnredirectWindow(QX11Info::display(),
                                e->window, CompositeRedirectManual);
                    pc->damageTracking(false);
                }
            } else {
                pc->damageTracking(true);
                XCompositeRedirectWindow(QX11Info::display(), e->window,
                                         CompositeRedirectManual);
            }
        } else
            damage_obj = XDamageCreate(dpy, e->window, XDamageReportNonEmpty);
    }
    // map early to give the app a chance to start drawing
    XMapWindow(dpy, e->window);
    XFlush(dpy);

    if (!pc && !(pc = getPropertyCache(e->window, 0, 0, damage_obj)))
        // Don't disturb the dead.  @damage_obj has been cleaned up.
        return;

    MCompAtoms::Type wtype = pc->windowType();
    QRect a = pc->realGeometry();
    int xres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
    int yres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;

    if (wtype == MCompAtoms::FRAMELESS || wtype == MCompAtoms::DESKTOP
        || wtype == MCompAtoms::INPUT) {
        if (a.width() != xres && a.height() != yres) {
            XResizeWindow(dpy, e->window, xres, yres);
            RESIZE(e->window, xres, yres);
        }
    }

    // Composition is enabled by default because we are introducing animations
    // on window map. It will be turned off once transitions are done
    if (!compositing && !pc->isInputOnly() && !device_state->displayOff()
        && (pc->hasAlphaAndIsNotOpaque() ||
            (wtype != MCompAtoms::INPUT &&
             pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_DIALOG))))
        enableCompositing();

    MDecoratorFrame *deco = MDecoratorFrame::instance();
    if (pc->isDecorator()) {
        MCompositeWindow *cw;
        deco->setDecoratorWindow(e->window);
        deco->setManagedWindow(0);

        if ((cw = getHighestDecorated())) {
            if (cw->status() == MCompositeWindow::Hung)
                deco->setManagedWindow(cw, true);
            else if (DECORATED_FS_WINDOW(cw->propertyCache()))
                deco->setManagedWindow(cw, true, true);
            else
                deco->setManagedWindow(cw);
        } else {
            STACKING("positionWindow 0x%lx -> bottom", e->window);
            positionWindow(e->window, false);
        }
        return;
    }

#ifdef WINDOW_DEBUG
    if (debug_mode) overhead_measure.start();
#endif

    if (FULLSCREEN_WINDOW(pc))
        fullscreen_wm_state(this, 1, e->window, pc);

    pc->setBeingMapped(true); // don't disable compositing & allow setting state
    const XWMHints &h = pc->getWMHints();
    if (pc->stackedUnmapped()) {
        Window d = desktop_window;
        if (d && stacking_list.indexOf(d) > stacking_list.indexOf(e->window))
            setWindowState(e->window, IconicState);
        else
            setWindowState(e->window, NormalState);
    } else if (pc->alwaysMapped() > 0 ||
               ((h.flags & StateHint) && (h.initial_state == IconicState)))
        setWindowState(e->window, IconicState);
    else
        setWindowState(e->window, NormalState);
    if (needDecoration(pc)) {
        if (!MDecoratorFrame::instance()->decoratorItem()) {
#ifdef ENABLE_BROKEN_SIMPLEWINDOWFRAME
         /* FIXME/TODO: this does NOT work when mdecorator starts
          * after the first decorated window is shown. See NB#196194. */
            if (!pc->isInputOnly())
                XAddToSaveSet(QX11Info::display(), e->window);
            // it will be non-toplevel, so mask needs to be set here
            XSelectInput(dpy, e->window,
                         StructureNotifyMask | ColormapChangeMask |
                         PropertyChangeMask);
            MSimpleWindowFrame *frame = 0;
            FrameData f = framed_windows.value(e->window);
            frame = f.frame;
            if (!frame) {
                frame = new MSimpleWindowFrame(e->window);
                Window trans = transient_for(e->window);
                if (trans)
                    frame->setDialogDecoration(true);

                // TEST: a framed translucent window
                if (pc->hasAlphaAndIsNotOpaque())
                    frame->setAttribute(Qt::WA_TranslucentBackground);
                QSize s = frame->suggestedWindowSize();
                XResizeWindow(QX11Info::display(), e->window, s.width(), s.height());
                RESIZE(e->window, s.width(), s.height());

                XReparentWindow(QX11Info::display(), frame->winId(),
                                RootWindow(QX11Info::display(), 0), 0, 0);

                // associate e->window with frame and its parent
                FrameData fd;
                fd.frame = frame;
                fd.mapped = true;
                fd.parentWindow = frame->winId();
                framed_windows[e->window] = fd;

                if (trans) {
                    FrameData f = framed_windows.value(trans);
                    if (f.frame) {
                        XSetTransientForHint(QX11Info::display(), frame->winId(),
                                             f.frame->winId());
                    }
                }
            }

            XReparentWindow(QX11Info::display(), e->window,
                            frame->windowArea(), 0, 0);
            MapRequesterPrivate::instance()->requestMap(pc);
            frame->show();

            XSync(QX11Info::display(), False);
#else // ! ENABLE_BROKEN_SIMPLEWINDOWFRAME
            qWarning("%s: mdecorator hasn't started yet", __func__);
#endif
        }
    }

    QHash<unsigned int, DismissedSplash>::iterator it = dismissedSplashScreens.find(pc->pid());
    if (it != dismissedSplashScreens.end()) {
        pc->setStackedUnmapped(true);
        setWindowState(e->window, IconicState);
        STACKING("positionWindow 0x%lx -> bottom", e->window);
        positionWindow(e->window, false);
        DismissedSplash &ds = it.value();
        ds.blockTimer.start();
    }
}

static Bool timestamp_predicate(Display *display, XEvent *xevent, XPointer arg)
{
    Q_UNUSED(arg);
    if (xevent->type == PropertyNotify &&
            xevent->xproperty.window == RootWindow(display, 0) &&
            xevent->xproperty.atom == ATOM(_NET_CLIENT_LIST))
        return True;
    return False;
}

Time MCompositeManager::getServerTime() const
{
    XEvent xevent;
    long data = 0;
    /* zero-length append to get timestamp in the PropertyNotify */
    XChangeProperty(QX11Info::display(), RootWindow(QX11Info::display(), 0),
                    ATOM(_NET_CLIENT_LIST),
                    XA_WINDOW, 32, PropModeAppend,
                    (unsigned char *)&data, 0);
    XIfEvent(QX11Info::display(), &xevent, timestamp_predicate, NULL);
    return xevent.xproperty.time;
}

/* NOTE: this assumes that stacking is correct */
void MCompositeManagerPrivate::checkInputFocus(Time timestamp)
{
    Window w = None;

    /* find topmost window wanting the input focus */
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
        Window iw = stacking_list.at(i);
        MWindowPropertyCache *pc = prop_caches.value(iw, 0);
        if (!pc || !pc->isMapped() || !pc->wantsFocus() || pc->isDecorator()
            || pc->isVirtual()
            || pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DOCK))
            continue;
        if (!pc->isOverrideRedirect() &&
            pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_INPUT)) {
            w = iw;
            break;
        }
        /* FIXME: do this based on geometry to cope with TYPE_NORMAL dialogs */
        /* don't focus a window that is obscured (assumes that NORMAL
         * and DESKTOP cover the whole screen) */
        if (!pc->isInputOnly() && !pc->isOverrideRedirect() &&
            (pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_NORMAL) ||
             pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DESKTOP)))
            break;
    }

    if (prev_focus == w)
        return;
    prev_focus = w;

    // timestamp is needed because Qt could set the focus some cases (i.e.
    // startup and XEmbed)
    if (timestamp == CurrentTime)
        timestamp = ((MCompositeManager*)qApp)->getServerTime();
#if 0 // disabled due to bugs in applications (e.g. widgetsgallery)
    MCompositeWindow *cw = windows.value(w);
    if (cw && cw->supportedProtocols().indexOf(ATOM(WM_TAKE_FOCUS)) != -1) {
        /* CurrentTime for WM_TAKE_FOCUS brings trouble
         * (a lesson learned from Fremantle) */
        if (timestamp == CurrentTime)
            timestamp = get_server_time();

        XEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.xclient.type = ClientMessage;
        ev.xclient.window = w;
        ev.xclient.message_type = ATOM(WM_PROTOCOLS);
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = ATOM(WM_TAKE_FOCUS);
        ev.xclient.data.l[1] = timestamp;

        XSendEvent(QX11Info::display(), w, False, NoEventMask, &ev);
    } else
#endif
    XSetInputFocus(QX11Info::display(), w, RevertToPointerRoot, timestamp);

    XChangeProperty(QX11Info::display(), RootWindow(QX11Info::display(), 0),
                    ATOM(_NET_ACTIVE_WINDOW),
                    XA_WINDOW, 32, PropModeReplace, (unsigned char *)&w, 1);
}

void MCompositeManagerPrivate::dirtyStacking(bool force_visibility_check,
                                             Time timestamp)
{
    if (timestamp != CurrentTime)
        stacking_timeout_timestamp = timestamp;
    if (force_visibility_check)
        stacking_timeout_check_visibility = true;
    if (!stacking_timer.isActive())
        stacking_timer.start();
}

void MCompositeManagerPrivate::pingTopmost()
{
    bool found = false, saw_desktop = false;
    // find out highest application window
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
         MCompositeWindow *cw;
         Window w = stacking_list.at(i);
         if (w == desktop_window) {
             saw_desktop = true;
             continue;
         }
         if (!(cw = COMPOSITE_WINDOW(w)) || cw->propertyCache()->isVirtual())
             continue;
         if (!found && !saw_desktop && cw->isMapped() && should_be_pinged(cw)) {
             cw->startPing();
             found = true;
             continue;
         } else if (!found && !saw_desktop && cw->isMapped()
                    && cw->isAppWindow(true))
             // topmost app does not support pinging, don't ping things under it
             found = true;
         cw->stopPing();
    }
}

void MCompositeManagerPrivate::setupButtonWindows(Window curr_app)
{
    Q_UNUSED(curr_app);
    MCompositeWindow *topmost = 0;
    // find out highest application window
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
         MCompositeWindow *cw;
         Window w = stacking_list.at(i);
         if (w == desktop_window)
             break;
         if (!(cw = COMPOSITE_WINDOW(w)))
             continue;
         if (cw->isMapped() && cw->isAppWindow(true)) {
             topmost = cw;
             break;
         }
    }
    bool home_set = false, close_set = false;
    static bool home_lowered = true, close_lowered = true;
    if (topmost) {
        XWindowChanges wc = {0, 0, 0, 0, 0, topmost->window(), Above};
        int mask = CWX | CWY | CWWidth | CWHeight | CWSibling | CWStackMode;
        const QRect &h = topmost->propertyCache()->homeButtonGeometry();
        if (h.width() > 1 && h.height() > 1) {
            wc.x = h.x(); wc.y = h.y();
            wc.width = h.width(); wc.height = h.height();
            XConfigureWindow(QX11Info::display(), home_button_win, mask, &wc);
            home_button_geom = h;
            home_set = true;
            home_lowered = false;
        }
        const QRect &c = topmost->propertyCache()->closeButtonGeometry();
        if (c.width() > 1 && c.height() > 1) {
            wc.x = c.x(); wc.y = c.y();
            wc.width = c.width(); wc.height = c.height();
            XConfigureWindow(QX11Info::display(), close_button_win, mask, &wc);
            close_button_geom = c;
            close_set = true;
            close_lowered = false;
        }
    }
    if ((home_set || close_set) && topmost) {
        buttoned_win = topmost->window();
        if (!home_set && !home_lowered) {
            XLowerWindow(QX11Info::display(), home_button_win);
            home_lowered = true;
        }
        if (!close_set && !close_lowered) {
            XLowerWindow(QX11Info::display(), close_button_win);
            close_lowered = true;
        }
    } else if (buttoned_win) {
        buttoned_win = 0;
        if (!close_lowered) {
            XLowerWindow(QX11Info::display(), close_button_win);
            close_lowered = true;
        }
        if (!home_lowered) {
            XLowerWindow(QX11Info::display(), home_button_win);
            home_lowered = true;
        }
    }
}

void MCompositeManagerPrivate::setCurrentApp(MCompositeWindow *cw,
                                             bool restacked)
{
    static Window prev = (Window)-1;
    Window w = cw ? cw->window() : desktop_window;
    if (prev == w) {
        if (restacked)
            // signal listener may need to restack its window
            emit currentAppChanged(current_app);
        return;
    }
    XChangeProperty(QX11Info::display(), RootWindow(QX11Info::display(), 0),
                    ATOM(_MEEGOTOUCH_CURRENT_APP_WINDOW),
                    XA_WINDOW, 32, PropModeReplace, (unsigned char *)&w, 1);
    current_app = w;
    emit currentAppChanged(current_app);
    if (!cw)
        cw = COMPOSITE_WINDOW(w);
    // tell orientation provider that there is no current window by passing 0
    orientationProvider.updateCurrentWindowOrienationAngle(cw ? cw->propertyCache() : 0);

    prev = w;
}

void MCompositeManagerPrivate::fixZValues()
{
    int last_i = stacking_list.size() - 1;

    // fix Z-values always to make sure we do it after an animation
    for (int i = 0; i <= last_i; ++i) {
        MCompositeWindow *witem = COMPOSITE_WINDOW(stacking_list.at(i));
        if (!witem)
            continue;
        if (witem->hasTransitioningWindow())
            // don't change Z values until animation is over
            break;
        witem->requestZValue(i);
    }

    // @notifs <- notification windows above which there are only
    // unmapped windows
    bool sg_above_notifs = false;
    QVector<MCompositeWindow *> notifs;
    for (int i = last_i; i >= 0; i--) {
        MCompositeWindow *witem = COMPOSITE_WINDOW(stacking_list.at(i));
        if (!witem)
            continue;
        if (!sg_above_notifs && witem->isMapped()) {
            if (witem->propertyCache()->windowType()
                == MCompAtoms::NOTIFICATION)
                notifs.prepend(witem);
            else if (witem->window() != home_button_win
                     && witem->window() != close_button_win) {
                sg_above_notifs = true;
            }
        }
        if (witem->hasTransitioningWindow())
            // don't change Z values until animation is over
            break;
    }

    // MCompositeWindowAnimation::ensureAnimationVisible() raises
    // the actors of the animated windows right above the highest
    // one in the stack.  Raise notifications even higher to make
    // them visible during animations.
    for (int i = 0; i < notifs.size(); i++)
        notifs[i]->requestZValue(last_i+1 + 2 + 1+i);
}

// index of the last visible window in stacking_list, or 0
int MCompositeManagerPrivate::indexOfLastVisibleWindow() const
{
    static int xres = ScreenOfDisplay(QX11Info::display(),
                               DefaultScreen(QX11Info::display()))->width;
    static int yres = ScreenOfDisplay(QX11Info::display(),
                               DefaultScreen(QX11Info::display()))->height;
    QRegion fs_r(0, 0, xres, yres);
    int last_i = stacking_list.size() - 1;

    for (int i = last_i; i >= 0; --i) {
         Window w = stacking_list.at(i);
         if (w == desktop_window)
             return i;
         MCompositeWindow *cw = COMPOSITE_WINDOW(w);
         MWindowPropertyCache *pc;
         if (!cw || !(pc = cw->propertyCache()) || !pc->isMapped()
             || cw->opacity() < 1.0
             || pc->hasAlphaAndIsNotOpaque() || pc->isInputOnly()
             || cw->isWindowTransitioning()
             // don't subtract hidden items during transition (example:
             // a hidden window between the desktop and swiped window)
             || (cw->hasTransitioningWindow() && !cw->isVisible()))
             continue;
         QRegion shape = pc->shapeRegion();
         shape.translate(pc->realGeometry().x(), pc->realGeometry().y());
         fs_r -= shape;
         if (fs_r.isEmpty())
             return i;
    }
    return 0;
}

bool MCompositeManagerPrivate::hasTransientVKB(MWindowPropertyCache *pc) const
{
    for (QList<Window>::const_iterator it = pc->transientWindows().begin();
         it != pc->transientWindows().end(); ++it) {
         MWindowPropertyCache *p = prop_caches.value(*it, 0);
         if (p && p->isMapped()
             && p->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_INPUT))
             return true;
    }
    return false;
}

void MCompositeManagerPrivate::sendSyntheticVisibilityEventsForOurBabies()
{
    int covering_i = indexOfLastVisibleWindow();
    Window duihome = desktop_window;
    int last_i = stacking_list.size() - 1;
    bool statusbar_visible = false;
    MWindowPropertyCache *ga_pc = 0;
    /* Send synthetic visibility events for our babies */
    int home_i = stacking_list.indexOf(duihome);
    for (int i = 0; i <= last_i; ++i) {
        MCompositeWindow *cw = COMPOSITE_WINDOW(stacking_list.at(i));
        if (!cw || !cw->isMapped() || !cw->propertyCache()) continue;
        if (device_state->displayOff()) {
            if (cw->propertyCache()->lowPowerMode() > 0
                && i >= covering_i) {
                cw->setWindowObscured(false);
                watch->keep_black = false;
            } else
                cw->setWindowObscured(true);
            // setVisible(false) is not needed because updates are frozen
            // and for avoiding NB#174346
            if (duihome && i >= home_i)
                setWindowState(cw->window(), NormalState);
            continue;
        }
        if (cw->isWindowTransitioning()) {
            // keep transitioning windows unobscured
            cw->setWindowObscured(false);
        } else if (i >= covering_i &&
            // don't expose a window that is hidden during transition
            // (visibility was set before by the animation)
            (!cw->hasTransitioningWindow() || cw->isVisible())) {
            cw->setWindowObscured(false);
            if (!cw->hasTransitioningWindow() && cw->paintedAfterMapping())
                cw->setVisible(true);
            if (!ga_pc && !cw->isWindowTransitioning() &&
                (cw->propertyCache()->globalAlpha() < 255 ||
                 cw->propertyCache()->videoGlobalAlpha() < 255))
                // select topmost window with global alpha properties
                ga_pc = cw->propertyCache();
            if (!statusbar_visible &&
                !cw->propertyCache()->statusbarGeometry().isEmpty())
                statusbar_visible = true;
        } else {
            if (i < home_i &&
                ((MTexturePixmapItem *)cw)->isDirectRendered()) {
                // make sure window below duihome is redirected
                ((MTexturePixmapItem *)cw)->enableRedirectedRendering();
                setWindowDebugProperties(cw->window());
            }
            if (!cw->propertyCache()->transientWindows().isEmpty()
                && hasTransientVKB(cw->propertyCache()))
                // keep it unobscured for self-compositing VKB
                cw->setWindowObscured(false);
            else
                cw->setWindowObscured(true);
            if (cw->window() != duihome)
                cw->setVisible(false);
        }
        // if !duihome, we use IconicState to stack windows to bottom
        if (duihome && i >= home_i)
            setWindowState(cw->window(), NormalState);
    }
    if (ga_pc)
        set_global_alpha(ga_pc->globalAlpha(), ga_pc->videoGlobalAlpha());
    else
        reset_global_alpha();

    // when there are transitioning windows statusbar visibility
    //is already true and we do not want to change this
    if (!MCompositeWindow::hasTransitioningWindow())
        setStatusbarVisibleProperty(statusbar_visible);

}

void MCompositeManagerPrivate::setStatusbarVisibleProperty(bool visiblity)
{
    static bool statusbar_currently_visible = false;
    if (statusbar_currently_visible != visiblity) {
        long v = visiblity ? 1 : 0;
        XChangeProperty(QX11Info::display(), wm_window,
                        ATOM(_MEEGOTOUCH_STATUSBAR_VISIBLE), XA_CARDINAL,
                        32, PropModeReplace, (unsigned char *)&v, 1);
        statusbar_currently_visible = visiblity;
    }
}

/* Go through stacking_list and verify that it is in order.
 * If it isn't, reorder it commit the changes to the server. */
void MCompositeManagerPrivate::checkStacking(bool force_visibility_check,
                                             Time timestamp)
{
    if (stacking_timer.isActive()) {
        if (stacking_timeout_check_visibility) {
            force_visibility_check = true;
            stacking_timeout_check_visibility = false;
        }
        stacking_timer.stop();
        stacking_timeout_timestamp = CurrentTime;
    }
    int last_i = stacking_list.size() - 1;
    MDecoratorFrame *deco = MDecoratorFrame::instance();

    int top_decorated_i;
    MCompositeWindow *highest_d = getHighestDecorated(&top_decorated_i);
    if (highest_d && !highest_d->isWindowTransitioning()) {
        if (highest_d->status() == MCompositeWindow::Hung)
            deco->setManagedWindow(highest_d, true);
        else if (DECORATED_FS_WINDOW(highest_d->propertyCache()))
            deco->setManagedWindow(highest_d, true, true);
        else
            deco->setManagedWindow(highest_d);
    } else if (!highest_d)
        deco->setManagedWindow(0);
    /* raise/lower decorator */
    if (highest_d && top_decorated_i >= 0 && deco->decoratorItem()
        && deco->managedWindow() == highest_d->window()
        && (DECORATED_FS_WINDOW(highest_d->propertyCache())
            || highest_d->status() == MCompositeWindow::Hung)) {
        // TODO: would be more robust to set decorator's managed window here
        // instead of in many different places in the code...
        Window deco_w = deco->decoratorItem()->window();
        int deco_i = stacking_list.indexOf(deco_w);
        if (deco_i >= 0) {
            if (deco_i < top_decorated_i) {
                STACKING_MOVE(deco_i, top_decorated_i);
                stacking_list.move(deco_i, top_decorated_i);
            } else {
                STACKING_MOVE(deco_i, top_decorated_i + 1);
                stacking_list.move(deco_i, top_decorated_i + 1);
            }
            if (!compositing)
                // decor requires compositing
                enableCompositing();
            deco->decoratorItem()->updateWindowPixmap();
            glwidget->update();
        }
    } else if ((!highest_d || top_decorated_i < 0) && deco->decoratorItem()) {
        Window deco_w = deco->decoratorItem()->window();
        int deco_i = stacking_list.indexOf(deco_w);
        if (deco_i > 0) {
            STACKING_MOVE(deco_i, 0);
            stacking_list.move(deco_i, 0);
        }
    }
    roughSort();

    // Focus is updated only if the set or the order of mapped windows
    // has changed.  _NET_CLIENT_LIST_STACKING may change even @only_mapped
    // didn't, if a window switched OR-ness or decorator-ness.
    //
    // only_mapped := grep(stacking_list,
    //                     witem && isMapped && !newlyMapped && !isClosing)
    // netClientListStacking := only_mapped - grep(stacking_list,
    //                     pc && isMapped && (isVirtual || isOR || isDeco)))
    static QVector<Window> prev_only_mapped;
    QVector<Window> only_mapped, netClientListStacking;
    bool mapped_order_changed;
    for (int i = 0; i <= last_i; ++i) {
        Window w = stacking_list[i];
        MCompositeWindow *witem = COMPOSITE_WINDOW(w);
        if (witem && witem->isMapped()
                && !(witem->isNewlyMapped() || witem->isClosing())) {
            only_mapped.append(stacking_list.at(i));
            MWindowPropertyCache *pc = witem->propertyCache();
            if (!(pc->isVirtual() || pc->isOverrideRedirect()
                  || pc->windowType() == MCompAtoms::DOCK
                  || pc->isDecorator()))
                // decorator and OR windows are not included in the property
                netClientListStacking.append(w);
        }
    }
    if ((mapped_order_changed = prev_only_mapped != only_mapped))
        prev_only_mapped = only_mapped;

    static QList<Window> prev_stacked;
    bool order_changed = prev_stacked != stacking_list;

    fixZValues();
    bool restacked = false;
    static bool xrestackwindows_error = false;
    if (xrestackwindows_error || order_changed) {
        xrestackwindows_error = !xserver_stacking.restack(stacking_list);
        if (xrestackwindows_error) {
            STACKING("XRestackWindows() failed, retry later");
            dirtyStacking(false);
        } else {
            prev_stacked = stacking_list;
            restacked = true;
        }
    }

    if (prevNetClientListStacking != netClientListStacking) {
        prevNetClientListStacking = netClientListStacking;
        XChangeProperty(QX11Info::display(),
                        RootWindow(QX11Info::display(), 0),
                        ATOM(_NET_CLIENT_LIST_STACKING),
                        XA_WINDOW, 32, PropModeReplace,
                        (unsigned char *)netClientListStacking.constData(),
                        netClientListStacking.size());
        if (desktop_window) {
            XPropertyEvent p;
            p.type   = PropertyNotify;
            p.window = RootWindow(QX11Info::display(), 0);
            p.atom   = ATOM(_NET_CLIENT_LIST_STACKING);
            p.state  = PropertyNewValue;
            XSendEvent(QX11Info::display(), desktop_window,
                       False, PropertyChangeMask, (XEvent *)&p);
        }
    }

    if (mapped_order_changed || changed_properties) {
        if (!device_state->displayOff())
            pingTopmost();
        checkInputFocus(timestamp);
    }
    if (mapped_order_changed || force_visibility_check || changed_properties)
        sendSyntheticVisibilityEventsForOurBabies();

    // current app has different semantics from getTopmostApp and pure isAppWindow
    MCompositeWindow *set_as_current_app = 0;
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
        Window w = stacking_list.at(i);
        if (!w) continue;
        MCompositeWindow *cw = COMPOSITE_WINDOW(w);
        if (!cw || !cw->propertyCache() || !cw->propertyCache()->is_valid
            || cw->propertyCache()->isVirtual())
            continue;
        if (cw->propertyCache()->winId() == desktop_window)
            break;
        Atom type = cw->propertyCache()->windowTypeAtom();
        if (type != ATOM(_NET_WM_WINDOW_TYPE_DIALOG) &&
            type != ATOM(_NET_WM_WINDOW_TYPE_MENU) &&
            cw->isMapped() && cw->isAppWindow(true)) {
            set_as_current_app = cw;
            break;
        }
    }
    setCurrentApp(set_as_current_app, restacked || changed_properties);
    changed_properties = false;
}

void MCompositeManagerPrivate::stackingTimeout()
{
    checkStacking(stacking_timeout_check_visibility,
                  stacking_timeout_timestamp);
    stacking_timeout_check_visibility = false;
    stacking_timeout_timestamp = CurrentTime;
    if (!device_state->displayOff() && !possiblyUnredirectTopmostWindow())
        enableCompositing();
}

// check if there is a categorically higher mapped window than pc
// iconic_is_ok: true if animation will raise the window above desktop
bool MCompositeManagerPrivate::skipStartupAnim(MWindowPropertyCache *pc,
                                               bool iconic_is_ok)
{
    if (pc->noAnimations())
        return true;
    // Ignore initial_state == IconicState if the client stacked the window
    // somewhere, then only skip if it's below the desktop (which is still
    // not correct but better than nothing).
    if (!iconic_is_ok && desktop_window && !pc->stackedUnmapped()) {
        const XWMHints &h = pc->getWMHints();
        if ((h.flags & StateHint) && h.initial_state == IconicState)
            return true;
    }
    // lock-screen window dont animate
    if (pc->isLockScreen() || pc->windowType() == MCompAtoms::NOTIFICATION)
        return true;
    
    bool above = false; // is the window above the desktop?
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
        MWindowPropertyCache *tmp;
        Window w = stacking_list.at(i);
        if (w == pc->winId())
            above = true;
        if (w && w == desktop_window) {
            if (iconic_is_ok)
                return false;
            // skip the animation if the window is below the desktop,
            // but not if the window hasn't been stacked, because then
            // the animation *would* raise it
            return !above && pc->stackedUnmapped();
        }
        if (!(tmp = prop_caches.value(w, 0)) || tmp->isInputOnly()
            || tmp == pc || !tmp->isMapped() || tmp->isDecorator())
            continue;
        if (tmp->meegoStackingLayer() > pc->meegoStackingLayer())
            return true;
    }
    return false;
}

void MCompositeManagerPrivate::mapEvent(XMapEvent *e, bool startup)
{
    Window win = e->window;

    if (win == xoverlay) {
        showOverlayWindow(true);
        return;
    }
    // NOTE: we send synthetic MapNotifys from redirectWindows()
    if (win == localwin || win == localwin_parent || win == close_button_win
        || e->send_event == True
        || (splash && win == splash->window())
        || win == home_button_win || e->event != QX11Info::appRootWindow())
        return;

    MWindowPropertyCache *pc = getPropertyCache(win);
    if (!pc || !pc->is_valid)
        return;

    pc->setBeingMapped(false);
    pc->setIsMapped(true);
    if (pc->isLockScreen()) {
        lockscreen_map_timer.stop();
        if (e->send_event == False)
            lockscreen_painted = false;
        else
            // we just started -> don't expect any damage
            lockscreen_painted = true;
    }

#ifdef ENABLE_BROKEN_SIMPLEWINDOWFRAME
    FrameData fd = framed_windows.value(win);
    if (fd.frame) {
        QRect a = pc->realGeometry();
        XConfigureEvent c;
        c.type = ConfigureNotify;
        c.send_event = True;
        c.event = win;
        c.window = win;
        c.x = 0;
        c.y = 0;
        c.width = a.width();
        c.height = a.height();
        c.border_width = 0;
        c.above = desktop_window;
        c.override_redirect = 0;
        XSendEvent(QX11Info::display(), c.event, True, StructureNotifyMask,
                   (XEvent *)&c);
    }
#endif // ENABLE_BROKEN_SIMPLEWINDOWFRAME

    MCompAtoms::Type wtype = pc->windowType();
    if (wtype == MCompAtoms::DESKTOP)
        desktop_window = win;

    MCompositeWindow *item = COMPOSITE_WINDOW(win);
    if (!compositing && item && item->needsCompositing())
        enableCompositing();

    bool new_item = !item;
    // only composite top-level windows
    if (!item && pc->parentWindow() == RootWindow(QX11Info::display(), 0))
        item = bindWindow(win, startup);

    if (item) {
        item->setPaintedAfterMapping(false); // not painted yet
        item->setIsMapped(true);
        if (!pc->isOverrideRedirect() && pc->windowType() != MCompAtoms::DOCK
            && !pc->isDecorator() && !pc->isVirtual())
            updateNetClientList(win, true);
        if (!new_item) {
            // reset item for the case previous animation did not end cleanly
            item->setUntransformed();
            item->setPos(pc->realGeometry().x(), pc->realGeometry().y());
        }
#ifdef WINDOW_DEBUG
        if (debug_mode)
            qDebug() << "Composition overhead (existing pixmap):"
                     << overhead_measure.elapsed();
#endif
        if (((MTexturePixmapItem *)item)->isDirectRendered()) {
            ((MTexturePixmapItem *)item)->enableRedirectedRendering();
            setWindowDebugProperties(item->window());
        } else
            item->saveBackingStore();
        if (startup) {
            item->setNewlyMapped(false);
            item->setVisible(true);
            item->setPaintedAfterMapping(true);
        } else if (!pc->alwaysMapped() && !pc->isInputOnly()
                   && (item->isAppWindow() || pc->invokedBy() != None)
                   && !skipStartupAnim(pc)) {
            item->setVisible(false); // keep it invisible until the animation
            if (!item->showWindow()) {
                item->setNewlyMapped(false);
                item->setVisible(true);
            }
        } else if (pc->isLockScreen()) {
            item->waitForPainting();
        } else {
            item->setNewlyMapped(false);
            if (pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DIALOG)) {
                item->setVisible(false);
                item->waitForPainting();
            } else if (pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_INPUT)
                       && item->hasTransitioningWindow()) {
                // there is an animation, don't show it unpainted on top
                item->setVisible(false);
                item->waitForPainting();
            } else if (!splash || !splash->matches(pc)) {
                item->setVisible(true);
                item->setPaintedAfterMapping(true);
            } else
                item->setPaintedAfterMapping(true);
        }
        // the current decorated window got mapped
        if (e->window == MDecoratorFrame::instance()->managedWindow() &&
            MDecoratorFrame::instance()->decoratorItem())
            MDecoratorFrame::instance()->decoratorItem()->setZValue(
                                                          item->zValue() + 1);
        setWindowDebugProperties(win);
    }

    if (!item || (e->event != QX11Info::appRootWindow())) {
        // only handle the MapNotify sent for the root window
        prop_caches.remove(win);
        delete pc;
        return;
    }

    bool stacked = false;
    const XWMHints &h = pc->getWMHints();
    if (pc->stackedUnmapped()) {
        stacked = true;
        Window d = desktop_window;
        if (d && stacking_list.indexOf(d) > stacking_list.indexOf(win))
            setWindowState(win, IconicState);
        else
            setWindowState(win, NormalState);
    } else if (pc->alwaysMapped() > 0 || (!startup && (h.flags & StateHint)
                                          && h.initial_state == IconicState)) {
        setWindowState(win, IconicState);
        STACKING("positionWindow 0x%lx -> bottom", win);
        positionWindow(win, false);
        stacked = true;
    }

    /* do this after bindWindow() so that the window is in stacking_list */
    if (pc->windowState() != IconicState &&
        (desktop_window != win || !getTopmostApp(0, win, true))) {
        bool activate = true;
        if (pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_INPUT)) {
            if (Window transient = pc->transientFor()) {
                MWindowPropertyCache *tpc = getPropertyCache(transient);
                if (tpc && tpc->isMapped()) {
                    // do not raise the transient parent with the input method
                    // window it is not necessarily supposed to be visible
                    activate = false;
                }
            }
        }
        if (activate)
            activateWindow(win, CurrentTime, false, stacked);
    }
    else {
        // desktop is stacked below the active application
        if (!stacked) {
            STACKING("positionWindow 0x%lx -> bottom", win);
            positionWindow(win, false);
        }
        if (win == desktop_window) {
            // lower always mapped windows below the desktop
            for (QHash<Window, MCompositeWindow *>::iterator it = windows.begin();
                 it != windows.end(); ++it) {
                 MCompositeWindow *i = it.value();
                 if (i->propertyCache() && i->propertyCache()->isMapped()
                     && i->propertyCache()->alwaysMapped() > 0)
                     setWindowState(i->window(), IconicState);
            }
            roughSort();
        }
    }

    if (dismissedSplashScreens.contains(pc->pid())) {
        DismissedSplash &ds = dismissedSplashScreens[pc->pid()];
        if (!ds.blockTimer.isValid())
            ds.blockTimer.start();
    } else if (splash && splash->matches(pc)) {
        waiting_damage = item;
    }

    dirtyStacking(false);
}

static bool should_be_pinged(MCompositeWindow *cw)
{
    MWindowPropertyCache *pc = cw->propertyCache();
    if (pc->supportedProtocols().contains(ATOM(_NET_WM_PING))
        && pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_NOTIFICATION)
        && pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_DOCK)
        && pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_MENU)
        && pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_INPUT)
        && !pc->isLockScreen()
        && !pc->isDecorator() && !pc->isOverrideRedirect()
        && pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_DESKTOP))
        return true;
    return false;
}

void MCompositeManagerPrivate::rootMessageEvent(XClientMessageEvent *event)
{
    MCompositeWindow *i = COMPOSITE_WINDOW(event->window);
    MWindowPropertyCache *pc = prop_caches.value(event->window, 0);

    if (event->message_type == ATOM(_NET_ACTIVE_WINDOW)) {
        QHash<unsigned int, DismissedSplash>::iterator it = dismissedSplashScreens.find(pc->pid());
        if (it != dismissedSplashScreens.end()) {
            DismissedSplash &ds = it.value();
            if (!ds.blockTimer.isValid()) {
                ds.blockTimer.start();
                // no XMapRequestEvent received yet - ignore event
                return;
            }
            else if (ds.blockTimer.elapsed() < 1000)
                return;
            else
                dismissedSplashScreens.erase(it);
        }
        if (pc && !pc->isMapped() && pc->beingMapped()) {
            // _NET_ACTIVE_WINDOW came between MapRequest and MapNotify,
            // mark it as "stacked unmapped" to ignore initial_state==Iconic
            STACKING("positionWindow 0x%lx -> botton", event->window);
            positionWindow(event->window, true);
            pc->setStackedUnmapped(true);
        } else if (pc && pc->isMapped()) {
            Window topmost = getTopmostApp();
            if (!m_extensions.values(MapNotify).isEmpty() || !topmost) {
                // Not necessary to animate if not in desktop view or we have a plugin.
                Window raise = event->window;
                bool needComp = false;
                if (!compositing && raise != desktop_window)
                    needComp = true;
                // Visibility notification to desktop window. Ensure this is sent
                // before transitions are started but after redirection
                if (event->window != desktop_window)
                    setExposeDesktop(false);
                if (i && (i->propertyCache()->windowState() == IconicState
                          // if it's not iconic, let the plugin decide
                          || (raise != topmost &&
                              !m_extensions.values(MapNotify).isEmpty()))) {
                    if (skipStartupAnim(i->propertyCache(), true)) {
                        STACKING("positionWindow 0x%lx -> top", i->window());
                        positionWindow(i->window(), true);
                    } else {
                        if (needComp)
                            enableCompositing();
                        i->restore();
                    }
                }
            } else if (event->window != desktop_window) {
                // unless we redirect the desktop we run the risk of using trash
                // in the animation because nothing is drawn there and the buffer
                // contents is undefined
                MCompositeWindow *d = COMPOSITE_WINDOW(desktop_window);
                if (d && ((MTexturePixmapItem *)d)->isDirectRendered()) {
                    ((MTexturePixmapItem *)d)->enableRedirectedRendering();
                    setWindowDebugProperties(d->window());
                }
                setExposeDesktop(false);
            }

#ifdef ENABLE_BROKEN_SIMPLEWINDOWFRAME
            FrameData fd = framed_windows.value(event->window);
            if (fd.frame)
                setWindowState(fd.frame->managedWindow(), NormalState);
            else
#endif // ENABLE_BROKEN_SIMPLEWINDOWFRAME
                setWindowState(event->window, NormalState);
            if (event->window == desktop_window) {
                // Mark normal applications on top of home Iconic to make our
                // qsort() function to work
                iconifyApps();
                activateWindow(event->window, CurrentTime, true);
            } else
                // use composition due to the transition effect
                activateWindow(event->window, CurrentTime, false);
        }
    } else if (i && pc && pc->isMapped()
               && event->message_type == ATOM(_NET_CLOSE_WINDOW)) {
        if (pc->noAnimations())
            closeHandler(i);
        else
            // save pixmap and delete or kill this window
            i->closeWindowRequest();
    } else if (event->message_type == ATOM(WM_PROTOCOLS)) {
        if (event->data.l[0] == (long) ATOM(_NET_WM_PING)) {
            MCompositeWindow *ping_source = COMPOSITE_WINDOW(event->data.l[2]);
            if (ping_source) {
                bool was_hung = ping_source->status() == MCompositeWindow::Hung;
                ping_source->receivedPing(event->data.l[1]);
                Q_ASSERT(ping_source->status() != MCompositeWindow::Hung);
                Window managed = MDecoratorFrame::instance()->managedWindow();
                if (was_hung && ping_source->window() == managed
                    && !ping_source->needDecoration()) {
                    // reset decorator's managed window & check compositing
                    dirtyStacking(false);
                } else if (was_hung && ping_source->window() == managed &&
                           DECORATED_FS_WINDOW(ping_source->propertyCache())) {
                    // ongoing call decorator
                    MDecoratorFrame::instance()->setOnlyStatusbar(true);
                }
            }
        }
    } else if (event->message_type == ATOM(_NET_WM_STATE)) {
        MWindowPropertyCache *pc = getPropertyCache(event->window);
        if (pc && event->data.l[1] == (long)ATOM(_NET_WM_STATE_SKIP_TASKBAR))
            skiptaskbar_wm_state(event->data.l[0], pc);
        else if (pc && event->data.l[1] == (long)ATOM(_NET_WM_STATE_FULLSCREEN))
            fullscreen_wm_state(this, event->data.l[0], event->window, pc);
    }
}

void MCompositeManagerPrivate::clientMessageEvent(XClientMessageEvent *event)
{
    // Handle iconify requests
    if (event->message_type == ATOM(WM_CHANGE_STATE))
        if (event->data.l[0] == IconicState && event->format == 32) {
            MCompositeWindow *i, *d_item;

            if (!(i = COMPOSITE_WINDOW(event->window)))
                return;
            if (!(d_item = COMPOSITE_WINDOW(desktop_window)))
                return;
            if (i == d_item)
                // not funny
                return;

            MWindowPropertyCache *pc = i->propertyCache();
            if (i->isMapped()
                && pc->windowState() == NormalState && !pc->dontIconify()
                && i->status() != MCompositeWindow::Minimizing) {
                if (pc->noAnimations()) {
                    // iconify all without the animation
                    iconifyApps();
                    checkStacking(false);
                    return;
                }
                Window lower, topmost = getTopmostApp();
                if (topmost)
                    lower = topmost;
                else
                    lower = event->window;
                if (pc->windowType() == MCompAtoms::NOTIFICATION
                    || pc->windowType() == MCompAtoms::INPUT) {
                    if (!topmost || !(i = COMPOSITE_WINDOW(topmost)))
                        return;
                    // Animate the iconification of the @topmost and
                    // leave the notification or the input window alone.
                }
                setExposeDesktop(false); // don't update thumbnails now

                bool needComp = !compositing;
                d_item->setVisible(true);

                // mark other applications on top of the desktop Iconified and
                // raise the desktop above them to make the animation end onto
                // the desktop
                int wi, lower_i = -1;
                int d_zeta = i->zValue() - 1;
                for (wi = stacking_list.size() - 1; wi >= 0; --wi) {
                    Window w = stacking_list.at(wi);
                    if (!w)
                        continue;

                    if (w == lower) {
                        lower_i = wi;
                        continue;
                    }
                    if (w == desktop_window)
                        break;
                    MCompositeWindow *cw = COMPOSITE_WINDOW(w);
                    if (cw && cw->isMapped() && (cw->isAppWindow(true)
                        // mark transient dialogs Iconic too, so that
                        // restoreHandler() is called when they are maximised
                        || (cw->propertyCache()->windowTypeAtom()
                               == ATOM(_NET_WM_WINDOW_TYPE_DIALOG)
                            && getLastVisibleParent(cw->propertyCache()))) &&
                        // skip devicelock and screenlock windows
                        !cw->propertyCache()->dontIconify())
                        setWindowState(cw->window(), IconicState);
                    else if (cw && cw->isMapped())
                        // show desktop under non-minimizable windows
                        d_zeta = cw->indexInStack() - 1;
                }
                // exposeSwitcher() can choose 'lower' so that lower_i < 0
                if (lower_i > 0) {
                    d_item->setZValue(d_zeta);
                    STACKING_MOVE(stacking_list.indexOf(desktop_window),
                                  lower_i - 1);
                    stacking_list.move(stacking_list.indexOf(desktop_window),
                                       lower_i - 1);

                    if (needComp)
                        enableCompositing();
                    if (skipStartupAnim(i->propertyCache())
                        || !i->iconify())
                        // signal will not come, set it iconic now
                        setWindowState(i->window(), IconicState);
                } else
                    setWindowState(i->window(), IconicState);
                i->stopPing();
            }
            return;
        }

    // Handle root messages
    rootMessageEvent(event);
}

void MCompositeManagerPrivate::closeHandler(MCompositeWindow *window)
{
    bool delete_sent = false;
    if (window->propertyCache()->supportedProtocols().contains(
                                                      ATOM(WM_DELETE_WINDOW))
        && window->status() != MCompositeWindow::Hung) {
        // send WM_DELETE_WINDOW message to the window that needs to close
        XEvent ev;
        memset(&ev, 0, sizeof(ev));

        ev.xclient.type = ClientMessage;
        ev.xclient.window = window->window();
        ev.xclient.message_type = ATOM(WM_PROTOCOLS);
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = ATOM(WM_DELETE_WINDOW);
        ev.xclient.data.l[1] = CurrentTime;

        XSendEvent(QX11Info::display(), window->window(), False,
                   NoEventMask, &ev);
        delete_sent = true;
        // the window has only some time to unmap even if it replies to the
        // pings unless it raises itself (which would normally mean asking the
        // user for confirmation etc.)
        window->startCloseTimer();
    }

    if (window->type() == MSplashScreen::Type) {
        MSplashScreen *splash = dynamic_cast<MSplashScreen*>(window);
        ::kill(splash->propertyCache()->pid(), SIGKILL);
        ::kill(-splash->propertyCache()->pid(), SIGKILL);
        dismissedSplashScreens.remove(splash->propertyCache()->pid());
    } else if (!delete_sent || window->status() == MCompositeWindow::Hung)
        kill_window(window);
    /* DO NOT deleteLater() this window yet because
       a) it can remove a mapped window from stacking_list
       b) delete can be ignored (e.g. "Do you want to exit?" dialog)
       c) _NET_WM_PID could be wrong (i.e. the window does not go away)
       d) we get UnmapNotify/DestroyNotify anyway when it _really_ closes */
}

// set all mapped transients of pc to either Normal or Iconic
void MCompositeManagerPrivate::setWindowStateForTransients(
                                        MWindowPropertyCache *pc,
                                        int state, int level)
{
    if (level > 10) {
        qWarning() << __func__ << "too deep recursion, give up";
        return;
    }
    Q_ASSERT(state == NormalState || state == IconicState);
    for (int i = 0; i < pc->transientWindows().size(); ++i) {
        MWindowPropertyCache *p = prop_caches.value(
                                       pc->transientWindows().at(i), 0);
        if (p && p->isMapped())
            setWindowState(p->winId(), state, level + 1);
    }
}

// window iconified or unmapping animation ended
void MCompositeManagerPrivate::lowerHandler(MCompositeWindow *window)
{
    // TODO: (work for more)
    // Handle minimize request coming from a managed window itself,
    // if there are any
#ifdef ENABLE_BROKEN_SIMPLEWINDOWFRAME
    FrameData fd = framed_windows.value(window->window());
    if (fd.frame) {
        setWindowState(fd.frame->managedWindow(), IconicState);
        MCompositeWindow *i = COMPOSITE_WINDOW(fd.frame->winId());
        if (i)
            i->iconify();
    }
#endif // ENABLE_BROKEN_SIMPLEWINDOWFRAME
    if (window->isMapped()) {
        // set for roughSort()
        setWindowState(window->window(), IconicState);
        roughSort();
    }

    // let MCompositeScene::drawItems() know @deco is useless
    // and not to be drawn before we had a chance to stack it
    // to the bottom
    MDecoratorFrame *deco = MDecoratorFrame::instance();
    if (deco->managedClient() == window)
        deco->setManagedWindow(0);

    // checkStacking() will redirect windows for the switcher
    dirtyStacking(false);
}

void MCompositeManagerPrivate::restoreHandler(MCompositeWindow *window)
{
    Window last = getLastVisibleParent(window->propertyCache());
    MCompositeWindow *to_stack;
    if (!last || !(to_stack = COMPOSITE_WINDOW(last)))
        to_stack = window;
    if (to_stack->propertyCache()->stackedUnmapped()) {
        Window d = desktop_window;
        if (d && stacking_list.indexOf(d) >
                 stacking_list.indexOf(to_stack->window()))
            setWindowState(to_stack->window(), IconicState);
        else
            setWindowState(to_stack->window(), NormalState);
    } else
        setWindowState(to_stack->window(), NormalState);

    // FIXME: call these for the whole transiency chain
    window->setNewlyMapped(false);
    if (to_stack != window)
        to_stack->setNewlyMapped(false);
    window->setUntransformed();
    if (window != to_stack)
        to_stack->setUntransformed();

    if (!to_stack->propertyCache()->stackedUnmapped()) {
        STACKING("positionWindow 0x%lx -> top", to_stack->window());
        positionWindow(to_stack->window(), true);
    }

    /* the animation is finished, compositing needs to be reconsidered */
    dirtyStacking(window->propertyCache()->opaqueWindow());
}

void MCompositeManagerPrivate::onFirstAnimationStarted()
{
    // make sure animations use up to date statusbar content
    setStatusbarVisibleProperty(true);
}

void MCompositeManagerPrivate::onAnimationsFinished(MCompositeWindow *window)
{
    fixZValues();
    if (window->propertyCache()->windowTypeAtom() ==
        ATOM(_NET_WM_WINDOW_TYPE_DESKTOP))
        /* desktop is on top, direct render it */
        possiblyUnredirectTopmostWindow();

    // call sendSyntheticVisibilityEventsForOurBabies() later so that the
    // plugin can do window stacking changes first
    QTimer::singleShot(0, this,
                       SLOT(sendSyntheticVisibilityEventsForOurBabies()));
}

void MCompositeManagerPrivate::setExposeDesktop(bool exposed)
{
    MCompositeWindow *cw;
    if (!desktop_window || !(cw = COMPOSITE_WINDOW(desktop_window)))
        return;
    cw->setWindowObscured(!exposed);
}

void MCompositeManagerPrivate::activateWindow(Window w, Time timestamp,
                                              bool disableCompositing,
                                              bool stacked)
{
    MWindowPropertyCache *pc = prop_caches.value(w, 0);
    if (!pc) return;

    if (pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_DESKTOP) &&
        pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_DOCK) &&
        !pc->isDecorator()) {
        MCompositeWindow *cw = COMPOSITE_WINDOW(w);
        // handle stacking on the animator for single (non-chained) window
        bool restoring = (cw && 
                          cw->propertyCache()->invokedBy() == None &&
                          cw->status() == MCompositeWindow::Restoring);
        if (!stacked) {
            // if this is a transient window, raise the "parent" instead
            Window last = getLastVisibleParent(pc);
            MWindowPropertyCache *to_stack = pc;
            if (last) to_stack = prop_caches.value(last, 0);
            // move it to the correct position in the stack
            STACKING("positionWindow 0x%lx -> top", to_stack->winId());
            if (!restoring)
                positionWindow(to_stack->winId(), true);
        }
        // possibly set decorator
        if (!restoring)
            dirtyStacking(false);
    } else if (pc->isDecorator()) {
        // if decorator crashes and reappears, stack it to bottom, raise later
        if (!stacked) {
            STACKING("positionWindow 0x%lx -> bottom", w);
            positionWindow(w, false);
        }
    } else if (w == desktop_window) {
        if (!stacked) {
            STACKING("positionWindow 0x%lx -> top", w);
            positionWindow(w, true);
        }
    } else if (pc->isMapped())
        checkInputFocus(timestamp);

    /* do this after possibly reordering the window stack */
    if (disableCompositing)
        dirtyStacking(false);
}

void MCompositeManager::lockScreenPainted()
{
    lockscreen_painted = true;
    if (!d->device_state->displayOff())
        d->displayOff(false);
}

MWindowPropertyCache *MCompositeManagerPrivate::findLockScreen() const
{
    for (QHash<Window, MWindowPropertyCache*>::const_iterator
         it = prop_caches.begin(); it != prop_caches.end(); ++it)
        if ((*it)->isLockScreen())
            return *it;
    return 0;
}

void MCompositeManagerPrivate::displayOff(bool display_off)
{
    if (display_off) {
        if (splash)
            splashTimeout();
        lockscreen_map_timer.stop();
        if (!haveMappedWindow())
            enableCompositing();
        MWindowPropertyCache *pc;
        MCompositeWindow *cw;
        if (!(pc = findLockScreen()) || !(cw = COMPOSITE_WINDOW(pc->winId()))
            || !pc->isMapped() || !cw->paintedAfterMapping())
            lockscreen_painted = false;
        // check whether there is a low-power mode window visible -- when the lockscreen is visible
        // we trust it to become the low-power mode window even if the flag is not yet set
        bool lpm_window = lockscreen_painted;
        if (!lpm_window) {
            int covering_i = indexOfLastVisibleWindow();
            for (int i = stacking_list.size() - 1; i >= covering_i; --i) {
                Window w = stacking_list[i];
                MWindowPropertyCache *pc = prop_caches.value(w, 0);
                if (pc && pc->isMapped() && pc->lowPowerMode() > 0) {
                    lpm_window = true;
                    break;
                }
            }
            if (!lpm_window) {
                watch->keep_black = true;
                if (!compositing)
                    enableCompositing();
                else
                    glwidget->update();
            }
        }
        /* stop pinging to save some battery */
        for (QHash<Window, MCompositeWindow *>::iterator it = windows.begin();
             it != windows.end(); ++it) {
             MCompositeWindow *i = it.value();
             i->stopPing();
             if (i->windowAnimator() && i->windowAnimator()->isActive())
                 i->windowAnimator()->finish();
             // stop damage tracking while the display is off
             if (i->propertyCache() &&
                 // don't disturb unmapped or waiting-for-damage lockscreen
                 (!i->propertyCache()->isLockScreen()
                  || (i->propertyCache()->isMapped()
                      && i->paintedAfterMapping())))
                 i->propertyCache()->damageTracking(false);
        }
    } else {
        MWindowPropertyCache *pc;
        if (device_state->touchScreenLock() == "locked" &&
            !lockscreen_painted && (pc = findLockScreen())) {
            // lockscreen not painted yet: wait for painting or timeout
            if (!pc->isMapped())
                // give it little time to map but not for ever
                lockscreen_map_timer.start();
            return;
        }
        watch->keep_black = false;
        glwidget->update();
        if (!possiblyUnredirectTopmostWindow() && !compositing)
            enableCompositing();
        /* start pinging again */
        pingTopmost();
        // restart damage tracking for redirected windows
        for (QHash<Window, MCompositeWindow *>::iterator it = windows.begin();
             it != windows.end(); ++it) {
             MCompositeWindow *i = it.value();
             MWindowPropertyCache *pc = i->propertyCache();
             if (!i->isDirectRendered() && pc &&
                 (pc->isMapped() || pc->beingMapped()))
                 pc->damageTracking(true);
        }
    }
    dirtyStacking(true);  // VisibilityNotify generation
}

void MCompositeManagerPrivate::callOngoing(bool ongoing_call)
{
    if (ongoing_call) {
        // if we have fullscreen app on top, set it decorated without resizing
        MCompositeWindow *cw = getHighestDecorated();
        if (cw && FULLSCREEN_WINDOW(cw->propertyCache())) {
            cw->setDecorated(true);
            MDecoratorFrame::instance()->setManagedWindow(cw, true, true);
        }
        dirtyStacking(false);
    } else {
        // remove decoration from fullscreen windows
        for (QHash<Window, MCompositeWindow *>::iterator it = windows.begin();
             it != windows.end(); ++it) {
            MCompositeWindow *i = it.value();
            if (FULLSCREEN_WINDOW(i->propertyCache()) && i->needDecoration())
                i->setDecorated(false);
        }
        MDecoratorFrame::instance()->setOnlyStatusbar(false);
        dirtyStacking(false);
    }
}

void MCompositeManagerPrivate::setWindowState(Window w, int state, int level)
{
    MWindowPropertyCache *pc = prop_caches.value(w, 0);
    if (pc && pc->windowState() == state) {
        if (state != WithdrawnState && !pc->transientWindows().isEmpty())
            setWindowStateForTransients(pc, state, level);
        return;
    }
    else if (pc && (!pc->isMapped() && !pc->beingMapped())
             && (state == NormalState || state == IconicState)) {
        // qWarning("%s: window 0x%lx is in wrong state", __func__, w);
        return;
    } else if (pc) {
        // cannot wait for the property change notification
        pc->setWindowState(state);
        if (state != WithdrawnState && !pc->transientWindows().isEmpty())
            setWindowStateForTransients(pc, state, level);
    }

    if (pc && pc->isVirtual())
        return;
    CARD32 d[2];
    d[0] = state;
    d[1] = None;

    XChangeProperty(QX11Info::display(), w, ATOM(WM_STATE), ATOM(WM_STATE),
                    32, PropModeReplace, (unsigned char *)d, 2);
}

void MCompositeManager::setWindowState(Window w, int state)
{
   d->setWindowState(w, state);
}

void MCompositeManager::queryDialogAnswer(unsigned int window, bool yes_answer)
{
    MCompositeWindow *cw = COMPOSITE_WINDOW(window);
    if (!cw || !cw->propertyCache() || !cw->propertyCache()->isMapped())
        return;
    if (yes_answer)
        d->closeHandler(cw);
    else
        cw->startDialogReappearTimer();
}

bool MCompositeManagerPrivate::x11EventFilter(XEvent *event, bool startup)
{
    // Core non-subclassable events
    static const int damage_ev = damage_event + XDamageNotify;
    static int shape_event_base = 0;
    if (!shape_event_base) {
        int i;
        if (!XShapeQueryExtension(QX11Info::display(), &shape_event_base, &i))
            qWarning("%s: no Shape extension!", __func__);
    }

    if (event->type == shape_event_base + ShapeNotify) {
        XShapeEvent *ev = (XShapeEvent*)event;
        if (ev->kind == ShapeBounding && prop_caches.contains(ev->window)) {
            MWindowPropertyCache *pc = prop_caches.value(ev->window);
            pc->shapeRefresh();
            glwidget->update();
            checkStacking(true); // re-check visibility
        }
        return true;
    }

    if (!startup)
        // Update our idea about @xserver_stacking.
        xserver_stacking.event(event);

    if (event->type != MapRequest && event->type != ConfigureRequest
        && processX11EventFilters(event, false))
        return true;

    if (event->type == damage_ev) {
        XDamageNotifyEvent *e = reinterpret_cast<XDamageNotifyEvent *>(event);
        damageEvent(e);
        return true;
    }

    bool ret = true;
    switch (event->type) {
    case FocusIn: {
        XFocusChangeEvent *e = (XFocusChangeEvent*)event;
        // make sure we focus right window on reverting the focus to root
        if (e->window == RootWindow(QX11Info::display(), 0)
            && e->mode == NotifyNormal) {
            prev_focus = e->window;
            checkInputFocus(CurrentTime);
        }
        break;
    }
    case DestroyNotify:
        destroyEvent(&event->xdestroywindow); break;
    case PropertyNotify:
        propertyEvent(&event->xproperty); break;
    case UnmapNotify:
        unmapEvent(&event->xunmap); break;
    case ConfigureNotify:
        configureEvent(&event->xconfigure); break;
    case ConfigureRequest:
        configureRequestEvent(&event->xconfigurerequest); break;
    case MapNotify:
        mapEvent(&event->xmap, startup); break;
    case MapRequest:
        mapRequestEvent(&event->xmaprequest); break;
    case ClientMessage:
        clientMessageEvent(&event->xclient); break;
    case ButtonRelease:
    case ButtonPress:
        buttonEvent(&event->xbutton);
        // TODO: enable this code when MSimpleWindowFrame raises from death.
        // ret = false;
        break;
    case MotionNotify: // in case a plugin subscribes these
        break;
    case KeyPress:
    case KeyRelease:
        XAllowEvents(QX11Info::display(), ReplayKeyboard, event->xkey.time);
        keyEvent(&event->xkey); break;
    case ReparentNotify:
        if (prop_caches.contains(((XReparentEvent*)event)->window)) {
            Window window = ((XReparentEvent*)event)->window;
            Window new_parent = ((XReparentEvent*)event)->parent;
            MWindowPropertyCache *pc = prop_caches.value(window);
            bool framed = false;
#ifdef ENABLE_BROKEN_SIMPLEWINDOWFRAME
            framed = framed_windows.contains(window);
#endif
            if (new_parent != pc->parentWindow()) {
                if (new_parent != QX11Info::appRootWindow() && !framed) {
                    // if new parent is not root/frame, forget about the window
                    if (!pc->isInputOnly()
                        && pc->parentWindow() != QX11Info::appRootWindow())
                        XRemoveFromSaveSet(QX11Info::display(), window);
                    MCompositeWindow *i = COMPOSITE_WINDOW(window);
                    if (i) i->deleteLater();
                    removeWindow(window);
                    prop_caches.remove(window);
                }
                pc->setParentWindow(new_parent);
            }
        }
        break;
    default:
        ret = false;
        break;
    }
    if (event->type != MapRequest && event->type != ConfigureRequest)
        processX11EventFilters(event, true);
    return ret;
}

bool MCompositeManagerPrivate::processX11EventFilters(XEvent *event, bool after)
{
    if (!m_extensions.contains(event->type))
        return false;

    QList<MCompositeManagerExtension*> evlist = m_extensions.values(event->type);
    bool processed = false;
    if (after)
        for (int i = 0; i < evlist.size(); ++i)
            evlist[i]->afterX11Event(event);
    else
        for (int i = 0; i < evlist.size(); ++i)
            processed = evlist[i]->x11Event(event);

    return processed;
}

void MCompositeManagerPrivate::keyEvent(XKeyEvent* e)
{
    if (e->state & Mod5Mask && e->keycode == switcher_key)
        exposeSwitcher();
}

void MCompositeManagerPrivate::buttonEvent(XButtonEvent* e)
{
    if (e->type == ButtonRelease && e->window == home_button_win
        && e->x >= 0 && e->y >= 0 && e->x <= home_button_geom.width()
        && e->y <= home_button_geom.height())
        exposeSwitcher();
    else if (e->type == ButtonRelease && buttoned_win
             && e->window == close_button_win && e->x >= 0 && e->y >= 0
             && e->x <= close_button_geom.width()
             && e->y <= close_button_geom.height()) {
        XClientMessageEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = ClientMessage;
        ev.window = buttoned_win;
        ev.message_type = ATOM(WM_PROTOCOLS);
        ev.format = 32;
        ev.data.l[0] = ATOM(WM_DELETE_WINDOW);
        ev.data.l[1] = CurrentTime;
        XSendEvent(QX11Info::display(), buttoned_win, False, NoEventMask,
                   (XEvent*)&ev);
        return;
    }
    if (buttoned_win) {
        XButtonEvent ev = *e;
        // synthetise the event to the application below
        ev.window = buttoned_win;
        XSendEvent(QX11Info::display(), buttoned_win, False,
                   e->type == ButtonPress ? ButtonPressMask
                                          : ButtonReleaseMask, (XEvent*)&ev);
    }
}

QGraphicsScene *MCompositeManagerPrivate::scene()
{
    return watch;
}

void MCompositeManagerPrivate::redirectWindows()
{
    XMapWindow(QX11Info::display(), xoverlay);

    QRect res = QApplication::desktop()->screenGeometry();
    foreach (Window win, xserver_stacking.getState()) {
        if (win == localwin || prop_caches.contains(win))
            continue;

        xcb_get_window_attributes_reply_t *attr;
        attr = xcb_get_window_attributes_reply(xcb_conn,
                     xcb_get_window_attributes(xcb_conn, win), 0);
        if (!attr || attr->_class == XCB_WINDOW_CLASS_INPUT_ONLY) {
            free(attr);
            continue;
        }

        xcb_get_geometry_reply_t *geom;
        geom = xcb_get_geometry_reply(xcb_conn,
                                xcb_get_geometry(xcb_conn, win), 0);
        if (!geom) {
            free(attr);
            continue;
        }

        // Pre-create MWindowPropertyCache for likely application windows
        MWindowPropertyCache *p = NULL;
        if (attr->map_state == XCB_MAP_STATE_VIEWABLE
            || (geom->width == res.width() && geom->height == res.height()))
            p = getPropertyCache(win, attr, geom);
        if (!p) {
            free(geom);
            free(attr);
            continue;
        }
        p->setParentWindow(RootWindow(QX11Info::display(), 0));

        if (attr->map_state == XCB_MAP_STATE_VIEWABLE &&
            // realGeomtry() doesn't block here because we initialized
            // the object with @geom (which we can't use anymore because
            // it's been free()d by the property cache.
            p->realGeometry().width() > 1 &&
            p->realGeometry().height() > 1) {
            // synthetise MapNotify, to use the usual code path for plugins
            XMapEvent e;
            memset(&e, 0, sizeof(e));
            e.type = MapNotify;
            e.event = RootWindow(QX11Info::display(), 0);
            e.window = win;
            x11EventFilter((XEvent*)&e, true);
            MCompositeWindow *w = COMPOSITE_WINDOW(win);
            if (w) {
                w->setNewlyMapped(false);
                w->setVisible(true);
            }
        }
    }

    // Wait for the MapNotify for the overlay (show() of the graphicsview
    // in main() causes it even if we don't map it explicitly)
    XEvent xevent;
    XIfEvent(QX11Info::display(), &xevent, map_predicate, (XPointer)xoverlay);
    showOverlayWindow(false);
    if (!possiblyUnredirectTopmostWindow())
        enableCompositing();
}

void MCompositeManagerPrivate::removeWindow(Window w)
{
    // Item is already removed from scene when it is deleted

    STACKING("remove 0x%lx from stack", w);
    if (windows.remove(w) | stacking_list.removeAll(w)
        | updateNetClientList(w, false))
        // Do remove @w from all these lists but only dirty the stacking
        // if something's been removed from any of them.
        dirtyStacking(false);

    if (desktop_window == w) desktop_window = 0;
}

Window MCompositeManager::desktopWindow() const
{
    return d->desktop_window;
}

bool MCompositeManager::debugMode() const
{
#ifdef WINDOW_DEBUG
    return debug_mode;
#else
    return false;
#endif
}

// Determine whether a decorator should be ordered above or below @win.
// If the answer is definite it is stored in *@cmpp and NULL is returned.
// Otherwise the decorator should be ordered exatly like the returned window,
// the decorator's managed window.
//
// Unused decorators should be below anything else.
// The decorated window should be below the decorator.
// Otherwise we don't know.
static bool compareDecorator(MWindowPropertyCache *win)
{
    MDecoratorFrame *deco;
    MCompositeWindow *man;

    if (!(deco = MDecoratorFrame::instance()))
        // the decorator is so unused that we don't even know about it
        return true;
    if (!(man = deco->managedClient()))
        // the decorator is unused
        return true;
    if (man->propertyCache() == win)
        // @win is the decorator's managed window, keep them together
        return false;
    return compareWindows(man->window(), win->winId());
}

// -1: pc_a is pc_b's ancestor; 1: pc_b is pc_a's ancestor; 0: no relation
static int transiencyRelation(MWindowPropertyCache *pc_a,
                              MWindowPropertyCache *pc_b)
{
    Window parent;
    bool pc_a_seen;
    MWindowPropertyCache *tmp, *pc_p;
    MCompositeManager *m = (MCompositeManager*)qApp;

    // if they are in the same circle => return 0
    pc_a_seen = false;
    for (tmp = pc_b; (parent = tmp->transientFor())
                      && (pc_p = m->propCaches().value(parent, 0)); tmp = pc_p)
        if (pc_p == pc_a)
            // We still need to verify pc_a and pc_b are not in circle.
            pc_a_seen = true;    
        else if (pc_p == pc_b) {
            if (pc_a_seen)
                return 0;
            break;
        }
    if (pc_a_seen)
        // pc_a is an ancestor of pc_b
        return -1;

    // pc_b could be in a circle, and it may be pc_a's ancestor
    for (tmp = pc_a; (parent = tmp->transientFor())
                     && (pc_p = m->propCaches().value(parent, 0)); tmp = pc_p)
        if (pc_p == pc_b)
            // pc_b is an ancestor of pc_a
            return 1;
       else if (pc_p == pc_a)
           // pc_a is in a transiency loop, which doesn't contain pc_b
           return 0;

    // pc_a and pc_b are unrelated
    return 0;
}

static float getLevel(MWindowPropertyCache *pc)
{
    MCompositeManager *cmgr = (MCompositeManager*)qApp;
    Window parent;
    float layer = pc->meegoStackingLayer();
    if (!layer && (parent = cmgr->getLastVisibleParent(pc))) {
        MWindowPropertyCache *pc_p = cmgr->propCaches().value(parent, 0);
        if (pc_p) layer = getLevel(pc_p);
    } else if (!layer && pc->windowType() == MCompAtoms::NOTIFICATION
               && !cmgr->getLastVisibleParent(pc))
        layer = 5.5;
    else if (!layer && !cmgr->getLastVisibleParent(pc) &&
             (pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_INPUT) ||
              pc->isOverrideRedirect() ||
              pc->netWmState().contains(ATOM(_NET_WM_STATE_ABOVE))))
        layer = 4;
    else if (!layer && !cmgr->getLastVisibleParent(pc) &&
             pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DIALOG)
             && MODAL_WINDOW(pc))
        layer = 0.5;
    return layer;
}

static int compareByLevel(MWindowPropertyCache *a, MWindowPropertyCache *b)
{
    float alayer = getLevel(a);
    float blayer = getLevel(b);
    float rel = alayer - blayer;
    if (rel < 0)
        // @a has lower stacking layer
        return 1;
    else if (rel > 0)
        // @a has greater stacking layer
        return -1;
    return 0;
}

static QList<Window> old_order;

// Internal qStableSort() comparator.  The desired rough order of
// @stacking_list roughly is:
//
// unused decorator (lowest), iconified/withdrawn windows possibly with
// decorator on top, desktop, normal state windows with transients/decorator
// on top, system-modal dialogs, input-type windows, notifications,
// windows with stacking layers (highest).
//
// Returns true if @w_a should definitely be below @w_b, otherwise false.
// This tells the sorting function that the sorting of @w_a is either
// greater than or equal to @w_b's.  In other words, @w_a needn't be
// below @w_a, but it could be, unless compareWindows(@w_b, @w_a) tells
// explicitly otherwise (ie. that @w_a needs to be higher than @w_b).
//
// TODO: before this can replace checkStacking(), we need to handle at least
// the decorator, possibly also window groups and dock windows.
static bool compareWindows(Window w_a, Window w_b)
{
    Atom type_a, type_b;
    int cmp;

    // qSort() should know better, but if it doesn't, tell it that
    // no item is less than itself.
    Q_ASSERT(w_a != w_b);

    MCompositeManager *cmgr = (MCompositeManager*)qApp;
    // If we don't know about either of the windows let them in peace
    // -- don't reason about what we don't know.
    MWindowPropertyCache *pc_a = cmgr->propCaches().value(w_a, 0);
    MWindowPropertyCache *pc_b = cmgr->propCaches().value(w_b, 0);
    if (!pc_a || !pc_b)
        SORTING(false, "NO PC");

    // if decorator and another transient are transient for the same
    // window, decorator (hung dialog) wins
    if (pc_a->isDecorator() && pc_a->transientFor()
        && pc_a->transientFor() == pc_b->transientFor())
        SORTING(false, "TR+DECO");
    else if (pc_b->isDecorator() && pc_b->transientFor()
             && pc_b->transientFor() == pc_a->transientFor())
        SORTING(true, "TR+DECO");
    // Mind decorators.  Lone decorators should go below everything else,
    // otherwise it's sorted above its managed window.
    if (pc_a->isDecorator())
        // @pc_a is a lone decorator or @pc_b happens to be
        // its managed window.
        SORTING( compareDecorator(pc_b), "DECO");
    else if (pc_b->isDecorator())
        // Likewise.
        SORTING(!compareDecorator(pc_a), "DECO");

    // Compare WM_STATEs of mapped windows
    if (pc_a->isMapped() && pc_b->isMapped()) {
        if (pc_a->windowState() != NormalState) {
            if (pc_b->windowState() == NormalState)
                // ...go below NormalState windows ...
                SORTING(true, "STATE");
            else
                // ... both are iconic.
                goto use_old_order;
        } else if (pc_b->windowState() != NormalState)
            // @pc_a is NormalState, @pc_b is not.
            SORTING(false, "STATE");
    } else {
        // "window state" of unmapped == whether it's below or above home
        int s_a, s_b;
        Window desk = cmgr->desktopWindow();
        if (!pc_a->isMapped())
            s_a = old_order.indexOf(desk) < old_order.indexOf(w_a) ?
                                                NormalState : IconicState;
        else
            s_a = pc_a->windowState();
        if (!pc_b->isMapped())
            s_b = old_order.indexOf(desk) < old_order.indexOf(w_b) ?
                                                NormalState : IconicState;
        else
            s_b = pc_b->windowState();
        if (s_a == IconicState && s_b == NormalState)
            SORTING(true, "STATE");
        else if (s_a == NormalState && s_b == IconicState)
            SORTING(false, "STATE");
        // take mapped transient parent into account if there is one
        Window p_a, p_b;
        p_a = pc_a->isMapped() ? cmgr->getLastVisibleParent(pc_a) : 0;
        p_b = pc_b->isMapped() ? cmgr->getLastVisibleParent(pc_b) : 0;
        if (p_a || p_b) {
            if (!p_a) p_a = w_a;
            if (!p_b) p_b = w_b;
            MWindowPropertyCache *ppc_a = cmgr->propCaches().value(p_a, 0);
            MWindowPropertyCache *ppc_b = cmgr->propCaches().value(p_b, 0);
            int cmp = compareByLevel(ppc_a, ppc_b);
            if (cmp > 0)
                SORTING(true, "MEEGOp");
            else if (cmp < 0)
                SORTING(false, "MEEGOp");

            if (old_order.indexOf(p_a) < old_order.indexOf(p_b))
                SORTING(true, "TRp");
            else
                SORTING(false, "TRp");
        }
        int cmp = compareByLevel(pc_a, pc_b);
        if (cmp > 0)
            SORTING(true, "MEEGO");
        else if (cmp < 0)
            SORTING(false, "MEEGO");

        goto use_old_order;
    }

    // Both @pc_a and @pc_b are in NormalState.
    // Sort the desktop below all NormalState windows.
    // (Quiz: why do we check @pc_b before @pc_a?
    //  Answer: to be consistent even if both windows are desktops.)
    type_b = pc_b->windowTypeAtom();
    if (type_b == ATOM(_NET_WM_WINDOW_TYPE_DESKTOP))
        SORTING(false, "DESK");
    type_a = pc_a->windowTypeAtom();
    if (type_a == ATOM(_NET_WM_WINDOW_TYPE_DESKTOP))
        SORTING(true, "DESK");

    // Sort a splashed window behind its splash screen.
    MCompositeWindow *cw, *splash;
    if ((cw = COMPOSITE_WINDOW(pc_a->winId()))
        && (splash = cmgr->splashed(cw))
        && splash->propertyCache() == pc_b)
        // @pc_a is splashed and @pc_b is the splash
        return true;
    else if ((cw = COMPOSITE_WINDOW(pc_b->winId()))
             && (splash = cmgr->splashed(cw))
             && splash->propertyCache() == pc_a)
        // v.v.
        return false;

    // Order transient windows below what they are transient for.
    // Since the sorting algorithm can infer that if trfor(@a) == @b
    // and trfor(@b) == @c then @a is transient for @c it is not
    // necessary for us to check if @pc_a is a grandparent of @pc_b
    // or vice versa.  However, we *do* have to mind circular
    // transiency between @pc_a and @pc_b otherwise we would
    // return true for both compareWindows(@w_a, @w_b) and
    // compareWindows(@w_b, @w_a), which would make the sorting
    // undeterministic.
    if (pc_b->transientFor() == w_a && pc_a->transientFor() != w_b)
      // @pc_b is transient for @pc_a, so it must be above it.
      SORTING(true, "TR");
    else if (pc_a->transientFor() == w_b && pc_b->transientFor() != w_a)
      // @pc_a is transient for @pc_b, so it must be above it.
      // NOTE: this test is necessary to avoid later tests returning true
      SORTING(false, "TR");
    else if (pc_b->transientFor() || pc_a->transientFor()) {
      // NOTE: if you touch this, check that test08.py and test21.py pass.
      int rel = transiencyRelation(pc_a, pc_b);
      if (rel < 0)
          // w_a is w_b's ancestor
          SORTING(true, "TR(anc.)");
      else if (rel > 0)
          // w_b is w_a's ancestor
          SORTING(false, "TR(anc.)");
      // take mapped transient parent into account if there is one
      Window p_a, p_b;
      p_a = cmgr->getLastVisibleParent(pc_a);
      p_b = cmgr->getLastVisibleParent(pc_b);
      if (p_a || p_b) {
          if (!p_a) p_a = w_a;
          if (!p_b) p_b = w_b;
          MWindowPropertyCache *ppc_a = cmgr->propCaches().value(p_a, 0);
          MWindowPropertyCache *ppc_b = cmgr->propCaches().value(p_b, 0);
          int cmp = compareByLevel(ppc_a, ppc_b);
          if (cmp > 0)
              SORTING(true, "MEEGOp");
          else if (cmp < 0)
              SORTING(false, "MEEGOp");

          if (old_order.indexOf(p_a) < old_order.indexOf(p_b))
              SORTING(true, "TRp");
          else
              SORTING(false, "TRp");
      }
    }

    // Compare by Meego stacking layers. Transients use the parent's layer.
    // NOTE: if you touch this, check that test21.py passes.
    cmp = compareByLevel(pc_a, pc_b);
    if (cmp > 0)
        SORTING(true, "MEEGO");
    else if (cmp < 0)
        SORTING(false, "MEEGO");
    // They're in the same layer.

use_old_order:
    // They didn't have any differential characteristic -- we need to
    // resort to the old order to know when to return true/false
    if (old_order.indexOf(w_a) < old_order.indexOf(w_b))
        SORTING(true, "OLD");
    else
        SORTING(false, "OLD");
}

void MCompositeManagerPrivate::roughSort()
{
    old_order = stacking_list;
    // Use a stable sorting algorithm to ensure roughSort() is invariant,
    // ie. that it keeps the order unless it is necessary to change.
    STACKING("sorting stack [%s]",
             dumpWindows(stacking_list).toLatin1().constData());
    qStableSort(stacking_list.begin(), stacking_list.end(), compareWindows);
    STACKING("resulting in: [%s]",
             dumpWindows(stacking_list).toLatin1().constData());
}

MCompositeWindow *MCompositeManagerPrivate::bindWindow(Window window,
                                                       bool startup)
{
    Display *display = QX11Info::display();

    // no need for StructureNotifyMask because of root's SubstructureNotifyMask
    XSelectInput(display, window, PropertyChangeMask);
    XShapeSelectInput(display, window, ShapeNotifyMask);

    MWindowPropertyCache *wpc = getPropertyCache(window);
    wpc->setIsMapped(true);
    MCompositeWindow *item = new MTexturePixmapItem(window, wpc);
    if (!item->isValid()) {
        item->deleteLater();
        return 0;
    }
    MWindowPropertyCache *pc = item->propertyCache();

    windows[window] = item;

    const XWMHints &h = pc->getWMHints();
    if (pc->stackedUnmapped()) {
        Window d = desktop_window;
        if (d && stacking_list.indexOf(d) > stacking_list.indexOf(window))
            setWindowState(window, IconicState);
        else
            setWindowState(window, NormalState);
    } else if (!startup && (h.flags & StateHint)
               && h.initial_state == IconicState) {
        setWindowState(window, IconicState);
        item->setZValue(-1);
    } else {
        if (!startup || pc->windowState() <= WithdrawnState)
            setWindowState(window, NormalState);
        item->setZValue(pc->windowType());
    }

    QRect a = pc->realGeometry();
    int fs_i = pc->netWmState().indexOf(ATOM(_NET_WM_STATE_FULLSCREEN));
    if (fs_i == -1) {
        pc->setRequestedGeometry(QRect(a.x(), a.y(), a.width(), a.height()));
    } else {
        int xres = ScreenOfDisplay(display, DefaultScreen(display))->width;
        int yres = ScreenOfDisplay(display, DefaultScreen(display))->height;
        pc->setRequestedGeometry(QRect(0, 0, xres, yres));
    }

    if (needDecoration(pc))
        item->setDecorated(true);

    item->updateWindowPixmap();

    int i = stacking_list.indexOf(window);
    if (i == -1) {
        STACKING("adding 0x%lx to stack", window);
        stacking_list.append(window);
    } else if (!pc->stackedUnmapped()) {
        STACKING_MOVE(i, stacking_list.size()-1);
        safe_move(stacking_list, i, stacking_list.size() - 1);
    }
    roughSort();

    addItem(item);

    if (pc->windowType() == MCompAtoms::INPUT) {
        dirtyStacking(false);
        return item;
    } else if (pc->windowType() == MCompAtoms::DESKTOP) {
        // just in case startup sequence changes
        desktop_window = window;
        orientationProvider.updateDesktopOrientationAngle(getPropertyCache(window));
        dirtyStacking(false);
        return item;
    }

    // the decorator got mapped. this is here because the compositor
    // could be restarted at any point
    if (pc->isDecorator() && !MDecoratorFrame::instance()->decoratorItem()) {
        // texture was already updated above
        MDecoratorFrame::instance()->setDecoratorItem(item);
    } else if (!device_state->displayOff())
        if (!splash || !splash->matches(pc))
            item->setVisible(true);

    dirtyStacking(false);

    emit windowBound(item);
    return item;
}

void MCompositeManagerPrivate::addItem(MCompositeWindow *item)
{
    watch->addItem(item);
    if (!item->propertyCache()->isVirtual())
        setWindowDebugProperties(item->window());

    connect(item, SIGNAL(firstAnimationStarted()),
                SLOT(onFirstAnimationStarted()));
    connect(item, SIGNAL(lastAnimationFinished(MCompositeWindow *)),
                SLOT(onAnimationsFinished(MCompositeWindow *)));
    if (item->propertyCache() && item->propertyCache()->windowType()
                                               == MCompAtoms::DESKTOP)
        return;

    connect(item, SIGNAL(itemRestored(MCompositeWindow *)), SLOT(restoreHandler(MCompositeWindow *)));
    connect(item, SIGNAL(itemIconified(MCompositeWindow *)), SLOT(lowerHandler(MCompositeWindow *)));
    connect(item, SIGNAL(closeWindowRequest(MCompositeWindow *)),
            SLOT(closeHandler(MCompositeWindow *)));


    // ping protocol
    connect(item, SIGNAL(windowHung(MCompositeWindow *, bool)),
            SLOT(gotHungWindow(MCompositeWindow *, bool)));
}

bool MCompositeManagerPrivate::updateNetClientList(Window w, bool addit)
{
    int idx;

    idx = netClientList.indexOf(w);
    if (addit != (idx < 0))
        return false;
    else if (addit)
        netClientList.append(w);
    else
        netClientList.remove(idx);

    XChangeProperty(QX11Info::display(),
                    RootWindow(QX11Info::display(), 0),
                    ATOM(_NET_CLIENT_LIST),
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)netClientList.constData(),
                    netClientList.size());
    if (desktop_window) {
        XPropertyEvent p;
        p.type   = PropertyNotify;
        p.window = RootWindow(QX11Info::display(), 0);
        p.atom   = ATOM(_NET_CLIENT_LIST);
        p.state  = PropertyNewValue;
        XSendEvent(QX11Info::display(), desktop_window,
                   False, PropertyChangeMask, (XEvent *)&p);
    }

    return true;
}

// mark application windows iconic (except those that shouldn't be)
void MCompositeManagerPrivate::iconifyApps()
{
    for (int wi = stacking_list.size() - 1; wi >= 0; --wi) {
        Window w = stacking_list.at(wi);
        MCompositeWindow *cw = COMPOSITE_WINDOW(w);
        if (cw && cw->propertyCache() && cw->propertyCache()->isMapped()
            && !cw->propertyCache()->dontIconify()
            && cw->isAppWindow(true))
            setWindowState(cw->window(), IconicState);
    }
    roughSort(); // sort iconic windows below the desktop
}

void MCompositeManager::iconifyApps()
{
    d->iconifyApps();
}

/*!
   Helper function to arrange arrange the order of the windows
   in the _NET_CLIENT_LIST_STACKING
*/
void MCompositeManagerPrivate::positionWindow(Window w, bool on_top)
{
    if (stacking_list.isEmpty())
        return;

    int wp = stacking_list.indexOf(w);
    if (wp == -1) {
        if (lastDestroyedSplash.window && lastDestroyedSplash.window == w) {
            // We handle a corner case here. The cross fade animation is shown
            // and splashTimeout() is called therefor. At the same time the
            // swipe animation is finished and therefor all windows are
            // iconified.
            // Now we must make sure to start the block timer - we know the window
            // is mapped in this case.
            DismissedSplash &ds = dismissedSplashScreens[lastDestroyedSplash.pid];
            if (!ds.blockTimer.isValid()) {
                ds.blockTimer.start();
            }
        }
        return;
    }

    if (on_top) {
        MWindowPropertyCache *pc = prop_caches.value(w, 0);
        //qDebug() << __func__ << "to top:" << w;
        if (pc && pc->isMapped()) {
            MCompositeWindow *cw = COMPOSITE_WINDOW(w);
            if (cw)
                cw->stopCloseTimer();
            setWindowState(w, NormalState);
            if (w == desktop_window)
                // iconify apps for roughSort()
                iconifyApps();
        }
        STACKING_MOVE(wp, stacking_list.size()-1);
        safe_move(stacking_list, wp, stacking_list.size() - 1);
        // needed so that checkStacking() finds the current application
        roughSort();
    } else {
        //qDebug() << __func__ << "to bottom:" << w;
        STACKING_MOVE(wp, 0);
        safe_move(stacking_list, wp, 0);
        // make sure it's not painted with the old Z value before the next
        // checkStacking() call, which sets the new Z value
        MCompositeWindow *i = COMPOSITE_WINDOW(w);
        if (i) i->requestZValue(-1);

        // lower the decorator also if it is decorated
        MDecoratorFrame *deco = MDecoratorFrame::instance();
        MCompositeWindow *d_item;
        if (deco && w == deco->managedWindow() &&
            (d_item = deco->decoratorItem()))
            d_item->requestZValue(-1);

        if (splash && w == splash->window()) {
            DismissedSplash &ds = dismissedSplashScreens[splash->propertyCache()->pid()];
            // check if the window has already been mapped -
            // block timer needs to be started in this case
            Window matchesPid = 0;
            foreach(MWindowPropertyCache *pc, prop_caches) {
                if (pc != splash->propertyCache()
                        && pc->pid() == splash->propertyCache()->pid()
                        && pc->isMapped()) {
                    matchesPid = pc->winId();
                    ds.blockTimer.start();
                    break;
                }
            }
            splashTimeout();
        }
    }

    dirtyStacking(false);
}

void MCompositeManager::expectResize(MCompositeWindow *cw, const QRect &r)
{
    XConfigureEvent xev;

    xev.window = cw->window();
    xev.x = r.x();
    xev.y = r.y();
    xev.width = r.width();
    xev.height = r.height();
    d->configureEvent(&xev, true);
    cw->propertyCache()->expectedGeometry() = r;
}

void MCompositeManager::positionWindow(Window w,
                                       MCompositeManager::StackPosition pos)
{
    d->positionWindow(w, pos == MCompositeManager::STACK_TOP ? true : false);
}

MCompositeWindow *MCompositeManager::decoratorWindow() const
{
    return MDecoratorFrame::instance()->decoratorItem();
}

const QRect &MCompositeManager::availableRect() const
{
    return MDecoratorFrame::instance()->availableRect();
}

void MCompositeManager::playFeedback(const QString &name) const
{
    MDecoratorFrame::instance()->playFeedback(name);
}

const QList<Window> &MCompositeManager::stackingList() const
{
    return d->stacking_list;
}

void MCompositeManagerPrivate::enableCompositing()
{
    if (!overlay_mapped)
        showOverlayWindow(true);
    else
        enableRedirection(true);
}

void MCompositeManagerPrivate::showOverlayWindow(bool show)
{   // don't change shapes if we're a unit test
    static bool first_call = true;
    static XRectangle empty = {0, 0, 0, 0},
                      fs = {0, 0,
                            ScreenOfDisplay(QX11Info::display(),
                                DefaultScreen(QX11Info::display()))->width,
                            ScreenOfDisplay(QX11Info::display(),
                                DefaultScreen(QX11Info::display()))->height};
    if (!show && (overlay_mapped || first_call)) {
        scene()->views()[0]->setUpdatesEnabled(false);
        if (localwin) { // do we own @xoverlay?
            XShapeCombineRectangles(QX11Info::display(), xoverlay,
                                    ShapeBounding, 0, 0, &empty, 1,
                                    ShapeSet, Unsorted);
            XShapeCombineRectangles(QX11Info::display(), localwin,
                                    ShapeBounding, 0, 0, &empty, 1,
                                    ShapeSet, Unsorted);
        }
        overlay_mapped = false;
    } else if (show && (!overlay_mapped || first_call)) {
#ifdef GLES2_VERSION
        enableRedirection(false);
#endif
        if (localwin) {
            XShapeCombineRectangles(QX11Info::display(), xoverlay,
                                    ShapeBounding, 0, 0, &fs, 1,
                                    ShapeSet, Unsorted);
            XShapeCombineRectangles(QX11Info::display(), localwin,
                                    ShapeBounding, 0, 0, &fs, 1,
                                    ShapeSet, Unsorted);
            XserverRegion r = XFixesCreateRegion(QX11Info::display(), &empty, 1);
            XFixesSetWindowShapeRegion(QX11Info::display(), xoverlay,
                                       ShapeInput, 0, 0, r);
            XFixesSetWindowShapeRegion(QX11Info::display(), localwin,
                                       ShapeInput, 0, 0, r);
            XFixesDestroyRegion(QX11Info::display(), r);
        }
        overlay_mapped = true;
#ifndef GLES2_VERSION
        enableRedirection(false);
#endif
        emit compositingEnabled();
    }
    first_call = false;
}

void MCompositeManagerPrivate::enableRedirection(bool emit_signal)
{
    // redirect from bottom to top
    for (int i = 0; i < stacking_list.size(); ++i) {
        Window w = stacking_list.at(i);
        MCompositeWindow *tp = COMPOSITE_WINDOW(w);
        if (tp && tp->isValid()
#ifdef WINDOW_DEBUG
            // some unit tests don't have MTexturePixmapItem
            && dynamic_cast<const MTexturePixmapItem *>(tp)
#endif
            && tp->isDirectRendered() && tp->propertyCache()
            && (tp->propertyCache()->isMapped()
                || tp->propertyCache()->beingMapped()
                || tp->isClosing()))
            ((MTexturePixmapItem *)tp)->enableRedirectedRendering();
        setWindowDebugProperties(w);
    }
    compositing = true;
    // no delay: application does not need to redraw when maximizing it
    scene()->views()[0]->setUpdatesEnabled(true);
    // NOTE: enableRedirectedRendering() calls glwidget->update() if needed
    if (emit_signal)
        // At this point everything should be rendered off-screen 
        emit compositingEnabled();        
}

void MCompositeManagerPrivate::gotHungWindow(MCompositeWindow *w, bool is_hung)
{
    MDecoratorFrame *deco = MDecoratorFrame::instance();
    if (is_hung)
        // re-add the window to the switcher in case the user
        // tried to close it by swiping and we made the window
        // skip the taskbar with the expectation that it would go
        w->propertyCache()->forceSkippingTaskbar(false);
    if (!mayShowApplicationHungDialog || !deco->decoratorItem())
        return;
    if (!is_hung) {
        deco->hideQueryDialog();
        return;
    }

    // own the window so we could kill it if we want to.
    enableCompositing();
    deco->showQueryDialog(w, true);
    dirtyStacking(false);

    // We need to activate the window as well with instructions to decorate
    // the hung window. Above call just seems to mark the window as needing
    // decoration
    activateWindow(w->window(), CurrentTime, false);
}

void MCompositeManagerPrivate::exposeSwitcher()
{
    MCompositeWindow *i = 0;
    for (int j = stacking_list.size() - 1; j >= 0; --j, i = 0) {
        Window w = stacking_list.at(j);
        if (w == desktop_window)
            // no windows to minimize
            return;
        if (!(i = COMPOSITE_WINDOW(w)) || !i->propertyCache() ||
            !i->propertyCache()->isMapped()
            || i->propertyCache()->windowState() == IconicState ||
            // skip devicelock and screenlock windows
            i->propertyCache()->dontIconify() || !i->isAppWindow(true))
            continue;
        break;
    }
    if (!i) return;

    XEvent e;
    e.xclient.type = ClientMessage;
    e.xclient.message_type = ATOM(WM_CHANGE_STATE);
    e.xclient.display = QX11Info::display();
    e.xclient.window = i->window();
    e.xclient.format = 32;
    e.xclient.data.l[0] = IconicState;
    e.xclient.data.l[1] = 0;
    e.xclient.data.l[2] = 0;
    e.xclient.data.l[3] = 0;
    e.xclient.data.l[4] = 0;
    // no need to send to X server first, also avoids NB#210587
    clientMessageEvent(&(e.xclient));
}

void MCompositeManagerPrivate::installX11EventFilter(long xevent,
                                                     MCompositeManagerExtension* extension)
{
    m_extensions.insert(xevent, extension);
}

void MCompositeManager::sighupHandler(int signo)
{
    Q_UNUSED(signo)
    char a = 1;
    ::write(sighupFd[0], &a, sizeof(a));
}

void MCompositeManager::handleSigHup()
{
    d->sighupNotifier->setEnabled(false);
    char tmp;
    ::read(sighupFd[1], &tmp, sizeof(tmp));
    reloadConfig();
    d->sighupNotifier->setEnabled(true);
}

#ifdef WINDOW_DEBUG
static void sigusr1_handler(int signo)
{
    Q_UNUSED(signo)
    debug_mode = !debug_mode;
}

template<class T>
static QString dumpWindows(const T &wins, bool leftToRight,
                           const char *sep, bool prefix)
{
    QString line;
    int nwins = wins.count();
    for (int i = 0; i < nwins; i++)
        line += QString().sprintf("%s0x%lx",
                                  i > 0 || prefix ? sep : "",
                                  wins[leftToRight ? i : nwins-1-i]);
    return line;
}

void MCompositeManager::dumpState(const char *heading)
{
    static const char *tf[] = { "false", "true" };
    static const char *yn[] = { "no", "yes" };
    int i;
    QString line;
    const QRect *r;
    MCompositeWindow *cw;
    QHash<const MCompositeManagerExtension*, QList<int> > extensions;

    if (heading)
      qDebug("%s: ", heading);

    qDebug(    "display:          %s",
               d->device_state->displayOff() ? "off" : "on");
    qDebug(    "call state:       %s",
               d->device_state->ongoingCall() ? "ongoing" : "idle");

    qDebug(    "composition:      %s", isCompositing() ? "on"  : "off");
    qDebug(    "xoverlay:         0x%lx, %s", d->xoverlay,
               d->overlay_mapped ? "mapped" : "unmapped");

    qDebug(    "current_app:      0x%lx", d->current_app);
    qDebug(    "topmostApp:       0x%lx (index: %d)",
               d->getTopmostApp(&i), i);
    if ((cw = d->getHighestDecorated()) != NULL)
        qDebug("highestDecorated: 0x%lx", cw->window());
    else
        qDebug("highestDecorated: None");

    qDebug(    "local_win:        0x%lx, parent: 0x%lx",
               d->localwin, d->localwin_parent);

    qDebug(    "prev_focus:       0x%lx", d->prev_focus);
    qDebug(    "buttoned_win:     0x%lx", d->buttoned_win);

    // Decoration button geometries.
    qDebug(    "decorated window: 0x%lx",
               MDecoratorFrame::instance()->managedWindow());
    r = &d->home_button_geom;
    qDebug(    "home button:      0x%lx (%dx%d%+d%+d)",
               d->home_button_win,
               r->width(), r->height(), r->x(), r->y());
    r = &d->close_button_geom;
    qDebug(    "close button:     0x%lx (%dx%d%+d%+d)",
               d->close_button_win,
               r->width(), r->height(), r->x(), r->y());

    // Stacking
    qDebug(    "stacking_timer:   %s",
               d->stacking_timer.isActive() ? "active" : "idle");
    qDebug(    "check_visibility: %s",
               tf[d->stacking_timeout_check_visibility]);

    // Top windows per stacking layer.
    qDebug("stacking layers:");
    qDebug("  desktop: 0x%lx", d->desktop_window);

    // Stacking order of mapped windows and mapping order of windows.
    qDebug("stacking_list (top->bottom): %s",
           dumpWindows(d->stacking_list, false,
                       "\n  ", true).toLatin1().constData());
    qDebug("mapping order (newest->oldest): %s",
           dumpWindows(d->netClientList, false).toLatin1().constData());
    qDebug("xserver_stacking (top->bottom): %s",
           dumpWindows(d->xserver_stacking.getState(), false,
                       "\n  ", true).toLatin1().constData());

    // All MCompositeWindow:s we know about.
    QHash<Window, MCompositeWindow *>::const_iterator cwit;
    qDebug("windows:");
    for (cwit = d->windows.constBegin(); cwit != d->windows.constEnd();
         ++cwit) {
        const QMetaObject mca = MCompAtoms::staticMetaObject;
        const QMetaEnum wintypes =
            mca.enumerator(mca.indexOfEnumerator("Type"));
        const QMetaObject mpc = MWindowPropertyCache::staticMetaObject;
        const QMetaEnum winstates =
            mpc.enumerator(mpc.indexOfEnumerator("WindowState"));
        const QMetaObject mcw = MCompositeWindow::staticMetaObject;
        const QMetaEnum appstates =
            mcw.enumerator(mcw.indexOfEnumerator("WindowStatus"));

        MCompositeWindow *behind;
        MCompositeWindow *cw = *cwit;
        Q_ASSERT(cwit.key() == cw->window());
        MWindowPropertyCache *pc = cw->propertyCache();

        // Determine the window's name.
        char *name = NULL;
        XFetchName(QX11Info::display(), cw->window(), &name);
        if (!name) {
            XClassHint cls;

            cls.res_name = cls.res_class = NULL;
            XGetClassHint(QX11Info::display(), cw->window(), &cls);
            if (!(name = cls.res_name))
                name = cls.res_class;
            else if (cls.res_class)
                XFree(cls.res_class);
        }

        // Get the PID and the command line of the process which created
        // the window.
        QByteArray cmdline;
        int pid = pc->pid();
        if (pid) {
            QFile f(QString().sprintf("/proc/%d/cmdline", pid));
            if (f.open(QIODevice::ReadOnly))
                cmdline = f.readLine(100);
        }
        if (cmdline.size() > 0)
            // The arguments are \0-delimited.
            cmdline = cmdline.replace('\0', ' ').trimmed();
        else
            cmdline = "<unknown PID>";

        qDebug("  ptr %p == xwin 0x%lx%s: %s", cw, cw->window(),
               cw->isValid() ? "" : " (not valid anymore)",
               name ? name : "[noname]");
        qDebug("    PID: %d, cmdline: %s", pid, cmdline.constData());
        qDebug("    mapped: %s, newly mapped: %s, stacked unmapped: %s",
               yn[cw->isMapped()], yn[cw->isNewlyMapped()],
               yn[pc->stackedUnmapped()]);
        qDebug("    InputOnly: %s, obscured: %s, direct rendered: %s",
               yn[pc->isInputOnly()], yn[cw->windowObscured()],
               yn[cw->isDirectRendered()]);
        qDebug("    window type: %s, is app: %s, needs decoration: %s",
               wintypes.valueToKey(pc->windowType()),
               yn[cw->isAppWindow()], yn[cw->needDecoration()]);
        qDebug("    status: %s, state: %s",
               appstates.valueToKey(cw->status()),
               winstates.valueToKey(pc->windowState()));
        qDebug("    has transitioning windows: %s, transitioning: %s, "
                   "closing: %s",
               yn[cw->hasTransitioningWindow()],
               yn[cw->isWindowTransitioning()],
               yn[cw->isClosing()]);
        qDebug("    stack index: %d, behind window: 0x%lx, "
                   "last visible parent: 0x%lx", cw->indexInStack(),
               (behind = cw->behind()) ? behind->window() : 0,
               cw->lastVisibleParent());

        // MWindowPropertyCache::transientFor() can change state,
        // transientWindows() doesn't.
        qDebug("    transients: %s",
               dumpWindows(pc->transientWindows()).toLatin1().constData());

        if (name)
            XFree(name);
    }

#ifdef ENABLE_BROKEN_SIMPLEWINDOWFRAME
    if (!d->framed_windows.isEmpty()) {
        QHash<Window, MCompositeManagerPrivate::FrameData>::const_iterator fwit;

        qDebug("framed_windows:");
        for (fwit = d->framed_windows.constBegin();
             fwit != d->framed_windows.constEnd(); ++fwit)
            qDebug("  0x%lx: parent=0x%lx, mapped=%s", fwit.key(),
                   fwit->parentWindow, fwit->mapped ? "yes" : "no");
    } else
        qDebug("framed_windows: <None>");
#endif // ENABLE_BROKEN_SIMPLEWINDOWFRAME

    // Which windows are in the property cache?
    line = "with property cache:";
    QHash<Window, MWindowPropertyCache*>::const_iterator pcit;
    for (pcit = d->prop_caches.constBegin();
         pcit != d->prop_caches.constEnd(); ++pcit)
      line += QString().sprintf(" 0x%lx", pcit.key());
    qDebug() << line.toLatin1().constData();

    // Dump the scene items from top to bottom.
    qDebug("scene:");
    foreach (const QGraphicsItem *gi, d->watch->items()) {
        if (!gi) {
            qDebug("  NULL (WTF?)");
            continue;
        }

        const QRectF &r = gi->sceneBoundingRect();
        const MCompositeWindow *cw = dynamic_cast<const MCompositeWindow *>(gi);
        qDebug("  %p: %dx%d%+d%+d %s", cw ? (void *)cw : (void *)gi,
                  (int)r.width(), (int)r.height(), (int)r.x(), (int)r.y(),
                  gi->isVisible() ? "visible" : "hidden");
    }

    // Tell how @xserver_stacking has been doing.
    qDebug("planner statistics:");
    qDebug("  conservative: %s",
            d->xserver_stacking.conStats.toString().toLatin1().constData());
    qDebug("  aggressive:   %s",
            d->xserver_stacking.altStats.toString().toLatin1().constData());

    // Show the current state of extensions.
    // @m_extenions is a QMultiHash of X events an extension reacts to
    // pointing to the object.  Invert the hash so we can iterate over
    // each extension once.
    qDebug("plugins:");
    for (QHash<int, MCompositeManagerExtension*>::const_iterator exit = d->m_extensions.constBegin(); exit != d->m_extensions.constEnd(); ++exit)
        extensions[*exit].append(exit.key());
    for (QHash<const MCompositeManagerExtension*, QList<int> >::const_iterator exit = extensions.constBegin(); exit != extensions.constEnd(); ++exit) {
        int event;
        bool first;
        QString events;

        // Print the extension's class name followed by its X events.
        first = true;
        foreach (event, *exit) {
            if (first)
                first = false;
            else
                events += ", ";

            // Translate common event numbers to strings.
            switch (event) {
            case ButtonPress:     events += "ButtonPress";    break;
            case ButtonRelease:   events += "ButtonRelease";  break;
            case MotionNotify:    events += "Motion";         break;
            case UnmapNotify:     events += "Unmap";          break;
            case MapNotify:       events += "Map";            break;
            case ConfigureNotify: events += "Configure";      break;
            case PropertyNotify:  events += "Property";       break;
            default:
                events += QString().sprintf("%u", event);
                break;
            }
        }
        qDebug("-- %s for event(s) %s:",
               exit.key()->metaObject()->className(),
               events.toLatin1().constData());
        exit.key()->dumpState();
    }
}

// Called when the remote control pipe has got input.
void MCompositeManager::remoteControl(int cmdfd)
{
    int lcmd;
    char cmd[128];

    if ((lcmd = ::read(cmdfd, cmd, sizeof(cmd)-1)) < 0)
        return;
    if (lcmd > 0 && cmd[lcmd-1] == '\n')
        lcmd--;
    cmd[lcmd] = '\0';

    if (!strcmp(cmd, "state")) {
        dumpState();
    } else if (!strncmp(cmd, "state ", strlen("state "))) {
        const char *space = &cmd[strlen("state")];
        dumpState(space+strspn(space, " "));
    } else if (!strcmp(cmd, "save")
               || !strncmp(cmd, "save ", strlen("save "))) {
        // dumpState() into a file
        static unsigned cnt = 0;
        int pos;
        time_t now;
        QString fname;
        const char *cfname;
        FILE *out, *saved_stderr;
        QRegExp rex("%(\\.\\d+)?[diuxX]");

        // Get the output file name.
        if ((cfname = strchr(cmd, ' ')) != NULL)
            cfname += strspn(cfname, " ");
        fname = cfname && *cfname ? cfname : "mc.log.%.2u";

        // Substitute @res with @cnt if necessary.
        if ((pos = rex.indexIn(fname)) >= 0)
            fname.replace(pos, rex.cap(0).length(),
                          QString().sprintf(rex.cap(0).toLatin1().constData(),
                                            cnt++));

        // Open @out.
        cfname = fname.toLatin1().constData();
        if (!(out = fopen(cfname, "w"))) {
            qDebug("couldn't open %s", cfname);
            return;
        }

        // Temporarily replace @stderr, so qDebug() will output to @out.
        saved_stderr = stderr;
        stderr = out;
        time(&now);
        dumpState(ctime(&now));
        stderr = saved_stderr;

        fclose(out);
        qDebug("state dumped into %s", fname.toLatin1().constData());
    } else if (!strcmp(cmd, "hang")) {
        MDecoratorFrame *deco = MDecoratorFrame::instance();
        MCompositeWindow *top = COMPOSITE_WINDOW(d->getTopmostApp());
        MCompositeWindow *man = deco ? COMPOSITE_WINDOW(deco->managedWindow())
                                     : NULL;

        if (man && man->isHung()) {
            qWarning("already hanging");
        } else if (!top) {
            qWarning("nothing to hang");
        } else {
            top->stopPing();
            top->hangIt();
            d->gotHungWindow(top, true);
        }
    } else if (!strcmp(cmd, "unhang")) {
        MDecoratorFrame *deco = MDecoratorFrame::instance();
        MCompositeWindow *man = deco ? COMPOSITE_WINDOW(deco->managedWindow())
                                     : NULL;

        if (man && man->isHung())
            // Restart pinging
            man->startPing(true);
        else
            qWarning("nothing to unhang");
    } else if (!strncmp(cmd, "say", strlen("say"))) {
        const char *what = &cmd[strlen("say")];
        if (*what++ == ' ')
            qDebug("%s", what);
    } else if (!strcmp(cmd, "debug")) {
        debug_mode = true;
        qDebug("debug mode on");
    } else if (!strcmp(cmd, "nodebug")) {
        debug_mode = false;
        qDebug("debug mode off");
    } else if (!strcmp(cmd, "reload")) {
        reloadConfig();
        qDebug("config reloaded");
    } else if (!strcmp(cmd, "restart")) {
        QString me = qApp->applicationFilePath();
        QStringList args = qApp->arguments();
        const char **argv;
        unsigned i;

        delete d;
#ifdef GLES2_VERSION
        eglTerminate(eglGetDisplay(EGLNativeDisplayType(EGL_DEFAULT_DISPLAY)));
        eglTerminate(eglGetDisplay(EGLNativeDisplayType(QX11Info::display())));
#endif
        XFlush(QX11Info::display());

        // Convert the QStringList of args into a char *[].
        i = 0;
        argv = new const char *[args.count()+1];
        foreach (const QString &arg, args)
            argv[i++] = qstrdup(arg.toLatin1().constData());
        argv[i] = NULL;

        // Restart ourselves.
        qDebug("Die, you son of a bitch!");
        execv(me.toLatin1().constData(), (char **)argv);
        qDebug("Gothca!");
    } else if (!strcmp(cmd, "exit") || !strcmp(cmd, "quit")) {
        // exit() deadlocks, go the fast route
        delete d;
        XFlush(QX11Info::display());
        _exit(0);
    } else if (!strcmp(cmd, "help")) {
        qDebug("Commands i understand:");
        qDebug("  state [<tag>]   dump MCompositeManager, MCompositeWindow:s ");
        qDebug("                  and QGraphicsScene state information");
        qDebug("  save [<fname>]  dump it into <fname>");
        qDebug("  hang            take it as if the topmost application hung");
        qDebug("  unhang          take it as if the hung application ponged");
        qDebug("  say <something> log <something>");
        qDebug("  debug, nodebug  turn the SIGUSR1 debug mode on/off");
        qDebug("  exit, quit      geez");
        qDebug("  restart         re-execute mcompositor");
        qDebug("  reload          reload the settings");
    } else
        qDebug("%s: unknown command", cmd);
}

void MCompositeManager::xtrace(const char *fun, const char *msg, int lmsg)
{
    MCompositeManager *p = static_cast<MCompositeManager *>(qApp);
    char str[160];

    // Normalize @fun and @msg so that @msg != NULL in the end,
    // and turn synopsis [2] into MCompositor::xtrace(NULL, msg).
    if (!msg) {
        if (fun) {
            msg = fun;
            fun = NULL;
        } else {
            msg = "HERE";
            lmsg = strlen("HERE");
        }
    }

    // Fail if we don't have an X connection yet.
    if (!p || !p->d || !p->d->xcb_conn) {
        qWarning("cannot xtrace yet from %s", fun ? fun : msg);
        return;
    }

    // Format @str to include both @fun and @msg if @fun was specified,
    // and count the length of @str.
    if (fun != NULL) {
        lmsg = snprintf(str, sizeof(str), "%s from %s", msg, fun);
        msg = str;
    } else if (lmsg < 0)
        lmsg = strlen(msg);

    // Make @str visible in xtrace by sending it along with an innocent
    // X request.  Unfortunately this makes this function a synchronisation
    // point (it has to wait for the reply).  Use xcb rather than libx11
    // because the latter maintains a hashtable of known Atom:s.
    free(xcb_intern_atom_reply(p->d->xcb_conn,
                               xcb_intern_atom(p->d->xcb_conn, False,
                                               lmsg, msg),
                               NULL));
}

void MCompositeManager::xtracef(const char *fun, const char *fmt, ...)
{
    va_list printf_args;
    char msg[160];
    int lmsg;

    va_start(printf_args, fmt);
    lmsg = vsnprintf(msg, sizeof(msg), fmt, printf_args);
    va_end(printf_args);
    xtrace(fun, msg, lmsg);
}
#endif // WINDOW_DEBUG

MCompositeManager::MCompositeManager(int &argc, char **argv)
    : QApplication(argc, argv)
{
    ensureSettingsFile();

    d = new MCompositeManagerPrivate(this);
    connect(d, SIGNAL(windowBound(MCompositeWindow*)), SIGNAL(windowBound(MCompositeWindow*)));
    d->mayShowApplicationHungDialog = !arguments().contains("-nohung");

    // Publish ourselves
    new MDecoratorFrame(this);

    // SIGHUP can be sent to force us to reload the configuration
    signal(SIGHUP, sighupHandler);
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sighupFd))
        qFatal("Couldn't create HUP socketpair");
    d->sighupNotifier = new QSocketNotifier(sighupFd[1],
                                            QSocketNotifier::Read, this);
    connect(d->sighupNotifier, SIGNAL(activated(int)),
            this, SLOT(handleSigHup()));

#ifdef WINDOW_DEBUG
    signal(SIGUSR1, sigusr1_handler);

    // Open the remote control interface.
    mknod("/tmp/mrc", S_IFIFO | 0666, 0);
    connect(new QSocketNotifier(open("/tmp/mrc", O_RDWR), QSocketNotifier::Read),
            SIGNAL(activated(int)), SLOT(remoteControl(int)));
#endif
}

MCompositeManager::~MCompositeManager()
{
    delete d;
    d = 0;
}

QHash<Window, MWindowPropertyCache*>& MCompositeManager::propCaches() const
{
    return d->prop_caches;
}

bool MCompositeManager::isEgl()
{
#ifdef GLES2_VERSION
    return true;
#else
    return false;
#endif
}

void MCompositeManager::setGLWidget(QGLWidget *glw)
{
    d->glwidget = glw;
}

QGLWidget *MCompositeManager::glWidget() const
{
    return d->glwidget;
}

QGraphicsScene *MCompositeManager::scene()
{
    return d->scene();
}

void MCompositeManager::prepareEvents()
{
    if (QX11Info::isCompositingManagerRunning()) {
        qCritical("Compositing manager already running.");
        ::exit(0);
    }

    d->xserver_stacking.syncState();
    d->watch->prepareRoot();
    d->prepare();
}

void MCompositeManager::loadPlugins(const QString &overridePluginPath,
                                    const QString &regularPluginDir)
{
    if (!overridePluginPath.isEmpty()) {
        d->loadPlugin(QString(overridePluginPath));
        return;
    }

    QDir pluginDir = QDir(regularPluginDir);
    if (pluginDir.exists() && !d->loadPlugins(pluginDir))
        qWarning("no plugins loaded");
}

bool MCompositeManager::hasPlugins() const
{
    return !d->m_extensions.isEmpty();
}

bool MCompositeManager::x11EventFilter(XEvent *event)
{
    return d->x11EventFilter(event);
}

void MCompositeManager::setSurfaceWindow(Qt::HANDLE window)
{
    d->localwin = window;
}

void MCompositeManager::redirectWindows()
{
    d->redirectWindows();
}

void MCompositeManager::enableCompositing(bool forced)
{
    Q_UNUSED(forced)
    d->enableCompositing();
}

bool MCompositeManager::isCompositing()
{
    return d->compositing;
}

void MCompositeManager::debug(const QString& d)
{
    const char* msg = d.toAscii();
    _log("%s\n", msg);
}

bool MCompositeManager::displayOff() const
{
    return d->device_state->displayOff();
}

MDeviceState &MCompositeManager::deviceState() const
{
    return *d->device_state;
}

MCompositeWindow *MCompositeManager::splashed(MCompositeWindow *cw) const
{
    return d->waiting_damage == cw ? d->splash : NULL;
}

bool MCompositeManager::possiblyUnredirectTopmostWindow()
{
    return d->possiblyUnredirectTopmostWindow();
}

void MCompositeManager::exposeSwitcher()
{
    d->exposeSwitcher();
}

static QHash<QString, QVariant> default_settings;

void MCompositeManager::config(char const *key, QVariant const &val) const
{
    if (!default_settings.contains(key))
        default_settings[key] = val;
}

QVariant MCompositeManager::config(char const *key) const
{
    if (settings->contains(key))
        return settings->value(key);
    Q_ASSERT(default_settings.contains(key));
    return default_settings.value(key);
}

int MCompositeManager::configInt(char const *key) const
{
    if (settings->contains(key))
        return settings->value(key).toInt();
    Q_ASSERT(default_settings.contains(key));
    return default_settings.value(key).toInt();
}

int MCompositeManager::configInt(char const *key, int defaultValue) const
{
    return settings->value(key, defaultValue).toInt();
}

void MCompositeManager::reloadConfig()
{
    settings->sync();
    if (settings->status() == QSettings::AccessError)
        qDebug() << __func__ << "can't access" << settings->fileName();
    else if (settings->status() == QSettings::FormatError)
        qDebug() << __func__ << "config file" << settings->fileName()
                 << "is in invalid format";
}

void MCompositeManager::ensureSettingsFile()
{
    // $HOME/.config/mcompositor/new-mcompositor.conf
    settings = new QSettings("mcompositor", "new-mcompositor", this);

    config("startup-anim-duration",             200);
    config("crossfade-duration",                250);
    config("switcher-keysym",           "BackSpace");
    config("ping-interval-ms",                 5000);
    config("hung-dialog-reappear-ms",         30000);
    config("damages-for-starting-anim",           2);
    config("damage-timeout-ms",                 500);
    config("expect-resize-timeout-ms",          800);
    config("splash-timeout-ms",               30000);
    config("lockscreen-map-timeout-ms",        1000);
    config("default-statusbar-height",           36);
    config("default-desktop-angle",             270);
    config("close-timeout-ms",                 5000);
    config("sheet-anim-duration",               350);
    config("chained-anim-duration",             500);
    config("callui-anim-duration",              400);
}
