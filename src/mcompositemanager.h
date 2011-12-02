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

#ifndef DUICOMPOSITEMANAGER_H
#define DUICOMPOSITEMANAGER_H

#include <QApplication>
#include <QGLWidget>
#include <QDir>
#include <QSettings>
#include <QTimer>
#include <mwindowpropertycache.h>
#include <QElapsedTimer>

class QGraphicsScene;
class MCompositeManagerPrivate;
class MCompAtoms;
class MCompositeWindow;
class MDeviceState;

// Encapsulates the details of grabbing the X server.
// While a grab is effective, only our commands are processed.
class MSGrabber: public QObject
{
    Q_OBJECT

public:
    MSGrabber();

    // grab() later when someone calls commit()
    void grabLater(bool setting = true);

    // grab() calls xlib immediately, but does not explicitly flush
    // the output queue.  Does nothing if the grab is already effective.
    // The grab is automatically released after a while unless reinforce()d.
    void grab();
    void reinforce();
    bool hasGrab() const { return has_grab; }
    bool grabDelayIsActive() const;

public slots:
    void commit();

    // It is OK to ungrab() an ungrabbed server.
    void ungrab();

protected:
    QTimer mercytimer;

private:
    QElapsedTimer timeSinceLastUngrab;
    QTimer delayedGrabTimer;
    // @needs_grab tells whether commit() should grab or ungrab.
    // After commit() these state variables should be equal.
    bool needs_grab, has_grab;

    friend class ut_Anim;
};

/*!
 * MCompositeManager is responsible for managing window events.
 *
 * It catches and redirects appropriate windows to offscreen pixmaps and
 * creates a MTexturePixmapItem object from these windows and adds them
 * to a QGraphicsScene. The manager also ensures the items are updated
 * when their contents change and removes them from its control when they are
 * destroyed.
 *
 */
class MCompositeManager: public QApplication
{
    Q_OBJECT
public:

    /*!
     * Initializes the compositing manager
     *
     * \param argc number of arguments passed from the command line
     * \param argv argument of strings passed from the command line
     */
    MCompositeManager(int &argc, char **argv);

    /*!
     * Cleans up resources
     */
    ~MCompositeManager();

    /*! Prepare and start composite management. This function should get called
     * after the window of this compositor is created and mapped to the screen
     */
    void prepareEvents();

    //! GLES2_VERSION or not
    static bool isEgl();

    /*! Specify the QGLWidget used by the QGraphicsView to draw the items on
     * the screen.
     *
     * \param glw The QGLWidget widget used in used by the scene's
     * QGraphicsView viewport
     */
    void setGLWidget(QGLWidget *glw);

    /*! QGLWidget accessor for static initialisations. */
    QGLWidget *glWidget() const;

    /*!
     * Reimplemented from QApplication::x11EventFilter() to catch X11 events
     */
    bool x11EventFilter(XEvent *event);

    /*!
     * Returns the scene where the items are rendered
     */
    QGraphicsScene *scene();

    /*!
     * Specifies the toplevel window where the items are rendered. This window
     * will reparented to the composite overlay window to ensure the compositor
     * stays on top of all windows.
     *
     * \param window Window id of the toplevel window where the items are
     * rendered. Typically, this will be the window id of a toplevel
     * QGraphicsView widget where the items are drawn
     */
    void setSurfaceWindow(Qt::HANDLE window);

    /*!
     * Redirects and manages existing windows as composited items
     */
    void redirectWindows();

    /*!
     * Mark windows iconic, except those that cannot be iconified.
     */
    void iconifyApps();

    /*!
     * Load @overridePluginPath if provided and abort if fails.
     * Otherwise, if there's no @overridePluginPath loads plugins
     * from @regularPluginDir but skips non-library files and
     * does not abort if there aren't plugins.
     */
    void loadPlugins(const QString &overridePluginPath,
                     const QString &regularPluginDir);
    
    /*!
     * Returns true if compositor has loaded any external plugins
     */
    bool hasPlugins() const;

    /*
     * Returns the current state of windows whether it is being composited
     * or not
     */
    bool isCompositing();

    /*!
     * Try to direct-render the topmost window
     */
    bool possiblyUnredirectTopmostWindow();

    /*!
     * Returns if the display is off
     */
    bool displayOff() const;

    /*!
     * Accessor for the MDeviceState object
     */
    MDeviceState &deviceState() const;

    /*!
     * Is a splash screen waiting for @cw?  If so, return it.
     */
    MCompositeWindow *splashed(MCompositeWindow *cw) const;

    void debug(const QString& d);
    QHash<Window, MWindowPropertyCache*>& propCaches() const;

    void expectResize(MCompositeWindow *cw, const QRect &r);
    enum StackPosition {
        STACK_BOTTOM = 0,
        STACK_TOP
    };
    void positionWindow(Window w, StackPosition pos);
    void setWindowState(Window, int);
    const QList<Window> &stackingList() const;
    Window getLastVisibleParent(MWindowPropertyCache *pc) const;
    Time getServerTime() const;
    MSGrabber servergrab;

    // called with the answer to mdecorator's dialog
    void queryDialogAnswer(unsigned int window, bool yes_answer);

    Window desktopWindow() const;
    bool debugMode() const;
    int configInt(const char *key) const;
    int configInt(const char *key, int defaultValue) const;
    QVariant config(const char *key) const;
    void config(const char *key, const QVariant &val) const;
    void reloadConfig();
    void recheckVisibility() const;
    void checkStacking(bool force_visibility_check,
                       Time timestamp = CurrentTime);

    /* Set GraphicsAlpha and/or VideoAlpha of the primary output
     * and enable/disable alpha blending if necessary. */
    void overrideGlobalAlpha(int new_gralpha, int new_vidalpha);
    void resetGlobalAlpha();

    // for MRestacker to ignore certain special windows
    bool ignoreThisWindow(Window) const;

    bool disableRedrawingDueToDamage() const;
    void setDisableRedrawingDueToDamage(bool);

#ifdef WINDOW_DEBUG
    // initialize MCompositeManager with an empty window stack
    void ut_prepare();
    // for adding and mapping a window from a unit test
    bool ut_addWindow(MWindowPropertyCache *pc, bool map_it = true);
    // for replacing MDeviceState object from a unit test
    void ut_replaceDeviceState(MDeviceState *d);
#endif

#ifdef REMOTE_CONTROL
    // Dump the current state of MCompositeManager and MCompositeWindow:s
    // to qDebug().  Only available if compiled with TESTABILITY=on
    // (-DWINDOW_DEBUG).
    void dumpState(const char *heading = 0);

    // "Print" @msg in xtrace, to show you where your program's control was
    // between the various X requests, responses and events.
    // Synopsis:
    // [1] MCompositeManager::xtrace();
    // [2] MCompositeManager::xtrace("HEI");
    // [3] MCompositeManager::xtrace(__PRETTY_FUNCTION__, "HEI");
    //
    // xtracef() is the same, except that it sends a formatted message.
    // You can leave @fun NULL if you want.
    static void xtrace (const char *fun = NULL, const char *msg = NULL,
                        int lmsg = -1);
    static void xtracef(const char *fun, const char *fmt, ...)
        __attribute__((format(printf, 2, 3)));
#endif

public slots:
    void enableCompositing(bool forced = false);

    /*!
     * Invoke to show the desktop window, possibly with switcher contents
     */
    void exposeSwitcher();

    /*!
     * Returns the decorator window or NULL.
     */
    MCompositeWindow *decoratorWindow() const;

    /*!
     * Area that is free after the area that decorator occupies.
     */
    const QRect &availableRect() const;

    /*!
     * Play a named feedback.
     */
    void playFeedback(const QString &name) const;

#ifdef REMOTE_CONTROL
    void remoteControl(int fd);
#endif

 signals:
    /*!
     * This signal is emitted the first time a window is bound as a
     * MCompositeWindow object
     */
    void windowBound(MCompositeWindow* window);

private slots:
    void lockScreenPainted();
    void handleSigHup();

private:
    void ensureSettingsFile();
    static void sighupHandler(int signo);
    MCompositeManagerPrivate *d;
    QSettings *settings;
    static int sighupFd[2];

    friend class MCompositeWindow;
    friend class MCompositeWindowAnimation;
    friend class MCompositeManagerExtension;
    friend class MTexturePixmapPrivate;
    friend class MWindowPropertyCache;
    friend class MCompositeWindowGroup;
    friend class MSplashScreen;
    friend class ut_Stacking;
    friend class ut_Anim;
    friend class ut_Lockscreen;
    friend class ut_CloseApp;
    friend class ut_Compositing;
    friend class ut_netClientList;
    friend class ut_splashscreen;
    friend class ut_PropCache;
};

#endif
