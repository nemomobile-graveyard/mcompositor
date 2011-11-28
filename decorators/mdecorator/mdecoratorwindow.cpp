/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (directui@nokia.com)
**
** This file is part of duicompositor.
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

#include <QtDebug>

#include <MSceneManager>
#include <MScene>
#include <MApplicationMenu>
#include <MApplicationPage>
#include <MNavigationBarView>
#include <MLabel>
#include <QGraphicsLinearLayout>
#include <mbutton.h>
#include <mwidgetaction.h>

#include <QApplication>
#include <QDesktopWidget>
#include <QX11Info>
#include <QGLFormat>
#include <QGLWidget>
#include <QLabel>
#include <QWindowStateChangeEvent>
#include <MFeedback>

#include "mdecoratorwindow.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xmd.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>

#include <mabstractdecorator.h>
#include <mabstractappinterface.h>
#include <mdesktopentry.h>
#include <mbuttonmodel.h>
#include <mdeviceprofile.h>

class MDecorator: public MAbstractDecorator
{
    Q_OBJECT
public:
    MDecorator(MDecoratorWindow *p)
        : MAbstractDecorator(p),
        decorwindow(p)
    {
    }

    void manageEvent(Qt::HANDLE window,
                             const QString &wmname,
                             M::OrientationAngle orient,
                             bool sbonly, bool hung)
    {
        decorwindow->managedWindowChanged(window, wmname, orient, hung);
        if (window)
            setOnlyStatusbar(sbonly);
    }

protected:

    void hideQueryDialog() {
        decorwindow->hideQueryDialog();
    }

    void setOnlyStatusbar(bool mode) 
    {
        decorwindow->setOnlyStatusbar(mode);
        decorwindow->setInputRegion();
        setAvailableGeometry(decorwindow->availableClientRect());
    }
    void playFeedback(const QString &name) {
        feedback.setName(name);
        feedback.play();
    }

private:

    MDecoratorWindow *decorwindow;
    MFeedback feedback;
};

class MDecoratorAppInterface : public MAbstractAppInterface
{
    Q_OBJECT

public:
    MDecoratorAppInterface(MDecoratorWindow *p)
        : MAbstractAppInterface(p)
        , decorwindow(p)
    {
    }

    void setManagedWindow(WId window)
    {
        currentWindow = window;
    }

    virtual void actionsChanged(QList<MDecoratorIPCAction> newMenu, WId window)
    {
        if (window != currentWindow)
            return;

        QList<MAction*> menu;
        actionHash.clear();

        foreach (MDecoratorIPCAction act, newMenu) {
            MAction* mact = createMAction(act);
            menu.append(mact);
        }
        decorwindow->addActions(menu);
    }

public slots:
    void actionTriggered(bool val)
    {
        if (!sender() || !qobject_cast<MAction*>(sender()))
            return;

        MAction* act = static_cast<MAction*>(sender());
        if (actionHash.contains(act))
            emit triggered(actionHash.value(act).id().toString(), val);
    }

    void actionToggled(bool val)
    {
        if (!sender() || !qobject_cast<MAction*>(sender()))
            return;

        MAction* act = static_cast<MAction*>(sender());
        if (actionHash.contains(act))
            emit toggled(actionHash.value(act).id().toString(), val);
    }

private:

    MAction* createMAction(const MDecoratorIPCAction& act)
    {
        //Normal MActions doesn't support custom QIcons, therefore we use MButtons and MWidgetAction

        MAction* mact;
        if (act.type() == MDecoratorIPCAction::MenuAction) {
            mact = new MAction(decorwindow);
            mact->setText(act.text());
            mact->setCheckable(act.isCheckable());
            mact->setChecked(act.isChecked());
            mact->setIcon(act.icon());
            mact->setLocation(MAction::ApplicationMenuLocation);
        } else {
            mact = new MWidgetAction(decorwindow);
            MButton* mbut = new MButton;
            mbut->setMinimumSize(0, 0);
            mbut->setObjectName("toolbaractioncommand");
            mbut->setIcon(act.icon());
            if (act.icon().isNull())
                mbut->setText(act.text());
            static_cast<MWidgetAction*>(mact)->setWidget(mbut);
            mact->setText(act.text());
            mact->setCheckable(act.isCheckable());
            mact->setChecked(act.isChecked());
            mact->setLocation(MAction::ToolBarLocation);

            updateViewAndStyling(mbut, false);

            if (act.isCheckable())
                connect(mbut, SIGNAL(toggled(bool)), mact, SIGNAL(toggled(bool)));
            else
                connect(mbut, SIGNAL(clicked(bool)), mact, SIGNAL(triggered(bool)));
        }
        connect(mact, SIGNAL(triggered(bool)), SLOT(actionTriggered(bool)));
        connect(mact, SIGNAL(toggled(bool)), SLOT(actionToggled(bool)));

        actionHash[mact] = act;
        return mact;
    }

    //This method is copied from libmeegotouch (MApplicationMenuView), but slightly changed
    void updateViewAndStyling(MButton *button, bool buttonGroup) const
    {
        QString toolBarButtonDefaultViewType = buttonGroup ? "toolbartab" : "toolbar";

        if (button && button->icon().isNull()) {
            // Only label -> could use different styling
            button->setTextVisible(true); //In this case we will show label (as it is all we have)
            if (button->viewType() != toolBarButtonDefaultViewType)
                button->setViewType(toolBarButtonDefaultViewType);
            button->setStyleName("ToolBarLabelOnlyButton");
        } else {
            if (button->viewType() != toolBarButtonDefaultViewType)
                button->setViewType(toolBarButtonDefaultViewType);
            button->setStyleName("ToolBarIconButton");
            button->setTextVisible(true);
        }
    }

    QHash<MAction*, MDecoratorIPCAction> actionHash;

    MDecoratorWindow *decorwindow;
    WId currentWindow;
};

MDecoratorWindow::MDecoratorWindow(QWidget *parent)
    : MApplicationWindow(parent),
      homeButtonPanel(0),
      escapeButtonPanel(0),
      navigationBar(0),
      statusBar(0),
      messageBox(0),
      managed_window(0),
      menuVisible(false)
{
    locale.installTrCatalog("recovery");
    locale.setDefault(locale);

    foreach (QGraphicsItem* item, items()) {
        MHomeButtonPanel *h;
        if (!homeButtonPanel && (h = dynamic_cast<MHomeButtonPanel*>(item))) {
            homeButtonPanel = h;
            continue;
        }
        MEscapeButtonPanel *e;
        if (!escapeButtonPanel &&
            (e = dynamic_cast<MEscapeButtonPanel*>(item))) {
            escapeButtonPanel = e;
            continue;
        }
        MNavigationBar *n;
        if (!navigationBar && (n = dynamic_cast<MNavigationBar*>(item))) {
            navigationBar = n;
            continue;
        }
    }
    // sometimes Libmeegotouch doesn't create statusbar at this point,
    // so create it ourselves and mark the window fullscreen to avoid duplicate
    statusBar = new MStatusBar;
    statusBar->setVisible(true);
    sceneManager()->appearSceneWindowNow(statusBar);
    setWindowState(windowState() | Qt::WindowFullScreen);

    // Check for presence of homeButtonPanel, navigationBar and statusBar
    if (!homeButtonPanel || !navigationBar || !statusBar)
        qFatal("Meego elements not found");

    homeButtonPanel = new MHomeButtonPanel();
    connect(homeButtonPanel, SIGNAL(buttonClicked()), this,
            SIGNAL(homeClicked()));
    if (escapeButtonPanel)
        connect(escapeButtonPanel, SIGNAL(buttonClicked()), this,
                SIGNAL(escapeClicked()));

    connect(navigationBar, SIGNAL(viewmenuTriggered()), SLOT(menuAppearing()));
    connect(navigationBar, SIGNAL(closeButtonClicked()), SIGNAL(escapeClicked()));

    requested_only_statusbar = false;

    d = new MDecorator(this);
    app = new MDecoratorAppInterface(this);

    connect(this, SIGNAL(homeClicked()), d, SLOT(minimize()));
    connect(this, SIGNAL(escapeClicked()), d, SLOT(close()));
    connect(sceneManager(),
            SIGNAL(orientationChanged(M::Orientation)),
            this,
            SLOT(screenRotated(M::Orientation)));

    setTranslucentBackground(true); // for translucent messageBox
    setBackgroundBrush(QBrush(QColor(0, 0, 0, 0)));
    setFocusPolicy(Qt::NoFocus);
    setSceneSize();
    setMDecoratorWindowProperty();
    setMeegotouchOpaqueProperty(true);
    setInputRegion();

    setProperty("animatedOrientationChange", false);
    setOrientationAngle(desktopOrientationAngle());
    setOrientationAngleLocked(true);
}

void MDecoratorWindow::managedWindowChanged(Qt::HANDLE w,
                                            const QString &title,
                                            M::OrientationAngle orient,
                                            bool hung)
{
    app->setManagedWindow(w);
    app->actionsChanged(QList<MDecoratorIPCAction>(), w);

    Qt::HANDLE old_window = managed_window;
    managed_window = w;
    if (messageBox && !hung)
        hideQueryDialog();

    if (!managed_window) {
        hideEverything();
        setProperty("animatedOrientationChange", false);
        setOrientationAngle(desktopOrientationAngle());
        return;
    }

    navigationBar->setViewMenuDescription(title);
    setProperty("animatedOrientationChange", !!old_window);
    setOrientationAngle(orient);
    if (hung) {
        createQueryDialog();
        if (isOnDisplay())
            // We can start showing it, otherwise wait.
            enterDisplayEvent();
    } else
        sceneManager()->appearSceneWindowNow(statusBar);
}

void MDecoratorWindow::createQueryDialog()
{
    QString name;

    if (messageBox)
        delete messageBox;

    XClassHint cls = {0, 0};
    XGetClassHint(QX11Info::display(), managed_window, &cls);
    if (cls.res_name) {
        name = QString(cls.res_name);
        if (name.endsWith(".launch"))
            // Remove the extension in order to find the .desktop file.
            name.resize(name.length()-strlen(".launch"));
        MDesktopEntry de(QString("/usr/share/applications/")
                         + name + ".desktop");
        if (de.isValid() && !de.name().isEmpty()) {
            name = de.name();

            // "If a translated string has many length variants, they are
            //  separated with U+009C (STRING TERMINATOR) character, and
            //  according to DTD for .ts file format, they should be ordered
            //  by decreasing length." (quote from mapplicationwindow.cpp)
            // This confuses the outlier and it decides it doesn't have space
            // for the "not responding" string.  Only keep the full string.
            int i = name.indexOf(QChar(0x9c));
            if (i >= 0)
                name.truncate(i);
        }
        XFree(cls.res_name);
    } else
        name.sprintf("window 0x%lx", managed_window);

    if (cls.res_class)
        XFree(cls.res_class);

    XSetTransientForHint(QX11Info::display(), winId(), managed_window);
    messageBox = new MMessageBox("", "", M::NoStandardButton);
    messageBox->setCentralWidget(new QGraphicsWidget(messageBox));
    QGraphicsLinearLayout *layout = new QGraphicsLinearLayout(Qt::Vertical,
                                            messageBox->centralWidget());
    MLabel *title = new MLabel(
                         qtTrId("qtn_reco_app_not_responding").arg(name),
                         messageBox);
    title->setStyleName("CommonQueryTitle");
    title->setWordWrap(true);
    title->setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    title->setAlignment(Qt::AlignCenter);
    layout->addItem(title);
    MLabel *text = new MLabel(qtTrId("qtn_reco_close_app_question"),
                              messageBox);
    text->setStyleName("CommonQueryText");
    text->setWordWrap(true);
    text->setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    text->setAlignment(Qt::AlignCenter);
    layout->addItem(text);
    messageBox->centralWidget()->setLayout(layout);
    MButtonModel *yes = messageBox->addButton(M::YesButton);
    MButtonModel *no = messageBox->addButton(M::NoButton);
    connect(yes, SIGNAL(clicked()), this, SLOT(yesButtonClicked()));
    connect(no, SIGNAL(clicked()), this, SLOT(noButtonClicked()));
}

void MDecoratorWindow::hideQueryDialog()
{
    if (!messageBox)
        return;

    XSetTransientForHint(QX11Info::display(), winId(), None);
    // do this even though messageBox may not be in the scene
    // (animation is not visible because of stacking changes)
    sceneManager()->disappearSceneWindow(messageBox);
    delete messageBox;
    messageBox = 0;

    restoreEverything();
    setInputRegion();
}

void MDecoratorWindow::enterDisplayEvent()
{   // If we've created the dialog it's time to show it.
    if (!messageBox
        || messageBox->sceneWindowState() != MSceneWindow::Disappeared)
        return;
    hideEverything();
    sceneManager()->appearSceneWindow(messageBox);
}

void MDecoratorWindow::leaveDisplayEvent()
{   // show_query_dialog was cancelled before we could do it
    hideQueryDialog();
}

void MDecoratorWindow::yesButtonClicked()
{
    d->queryDialogAnswer(managed_window, true);
    hideQueryDialog();
}

void MDecoratorWindow::noButtonClicked()
{
    d->queryDialogAnswer(managed_window, false);
    hideQueryDialog();
}

void MDecoratorWindow::setOnlyStatusbar(bool mode, bool temporary)
{
    if (mode || messageBox) {
        sceneManager()->disappearSceneWindowNow(navigationBar);
        sceneManager()->disappearSceneWindowNow(homeButtonPanel);
        if (escapeButtonPanel)
            sceneManager()->disappearSceneWindowNow(escapeButtonPanel);
    } else {
        sceneManager()->appearSceneWindowNow(navigationBar);
        sceneManager()->appearSceneWindowNow(homeButtonPanel);
        if (escapeButtonPanel)
            sceneManager()->appearSceneWindowNow(escapeButtonPanel);
    }

    if (!temporary)
        requested_only_statusbar = mode;
    only_statusbar = mode;
}

void MDecoratorWindow::hideEverything()
{
    setOnlyStatusbar(true, true);
    sceneManager()->disappearSceneWindowNow(statusBar);
}

void MDecoratorWindow::restoreEverything()
{
    setOnlyStatusbar(requested_only_statusbar);
    sceneManager()->appearSceneWindowNow(statusBar);
}

void MDecoratorWindow::screenRotated(const M::Orientation &orientation)
{
    Q_UNUSED(orientation);
    setInputRegion();
    d->setAvailableGeometry(availableClientRect());
}

void MDecoratorWindow::setInputRegion()
{
    static QRegion prev_region;
    QRegion region;
    const QRegion fs(QApplication::desktop()->screenGeometry());
    // region := decoration region
    if (messageBox || menuVisible) {
        // Occupy all space.
        region = fs;
        setMeegotouchOpaqueProperty(false);
    } else {
        // Decoration includes the status bar, and possibly other elements.
        QRect sbrect = statusBar->sceneBoundingRect().toRect();

        // work around Libmeegotouch lying about the size
        if (sbrect.height() == 51)
            sbrect.setHeight(36);
        if (sbrect.width() == 51)
            sbrect.setWidth(36);

        region = sbrect;
        bool translate = true;
        int angle = sceneManager()->orientationAngle();
        if (angle == 270 && sbrect.x() == 0 && sbrect.y() == 0
            && sbrect.width() < sbrect.height())
            // SB rect is already in screen coordinates (fixes NB#275508)
            translate = false;

        if (!only_statusbar) {
            region += navigationBar->sceneBoundingRect().toRect();
            region += homeButtonPanel->sceneBoundingRect().toRect();
            if (escapeButtonPanel)
                region += escapeButtonPanel->sceneBoundingRect().toRect();
        }

        // The coordinates we receive from libmeegotouch are rotated
        // by @angle.  Map @retion back to screen coordinates.
        if (translate && angle != 0) {
            QTransform trans;
            const QRect fs(QApplication::desktop()->screenGeometry());

            trans.rotate(angle);
            if (angle == 270)
                trans.translate(-fs.height(), 0);
            else if (angle == 180)
                trans.translate(-fs.width(), -fs.height());
            else if (angle == 90)
                trans.translate(0, -fs.width());
            region = trans.map(region);
        }
        setMeegotouchOpaqueProperty(true);
    }

    // Set our input and bounding shape to @region if changed.
    if (prev_region != region) {
        prev_region = region;

        // Convert @region to @xrects.
        XRectangle *xrects;
        const QVector<QRect> rects = region.rects();
        int nxrects = rects.count();
        xrects = new XRectangle[nxrects];
        for (int i = 0; i < nxrects; ++i) {
            xrects[i].x = rects[i].x();
            xrects[i].y = rects[i].y();
            xrects[i].width = rects[i].width();
            xrects[i].height = rects[i].height();
        }

        Display *dpy = QX11Info::display();
        XserverRegion shapeRegion = XFixesCreateRegion(dpy, xrects, nxrects);
        delete[] xrects;
        XFixesSetWindowShapeRegion(dpy, winId(), ShapeInput,
                                   0, 0, shapeRegion);
        XFixesSetWindowShapeRegion(dpy, winId(), ShapeBounding,
                                   0, 0, shapeRegion);

        XFixesDestroyRegion(dpy, shapeRegion);
    }

    // The rectangle available for the application is the largest square
    // on the screen not covered by decoration completely.
    availableRect = (fs - region).boundingRect();
}

void MDecoratorWindow::setSceneSize()
{
    // always keep landscape size
    Display *dpy = QX11Info::display();
    int xres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
    int yres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;
    scene()->setSceneRect(0, 0, xres, yres);
    setMinimumSize(xres, yres);
    setMaximumSize(xres, yres);
}

void MDecoratorWindow::setMDecoratorWindowProperty()
{
    long on = 1;

    XChangeProperty(QX11Info::display(), winId(),
                    XInternAtom(QX11Info::display(),
                                "_MEEGOTOUCH_DECORATOR_WINDOW", False),
                    XA_CARDINAL,
                    32, PropModeReplace,
                    (unsigned char *) &on, 1);
}

void MDecoratorWindow::setMeegotouchOpaqueProperty(bool enable)
{
    static long prev = -1;
    long new_value = enable ? 1 : 0;

    if (prev != new_value) {
        XChangeProperty(QX11Info::display(), winId(),
                        XInternAtom(QX11Info::display(),
                                    "_MEEGOTOUCH_OPAQUE_WINDOW", False),
                        XA_CARDINAL,
                        32, PropModeReplace,
                        (unsigned char *) &new_value, 1);
        prev = new_value;
    }
}

M::OrientationAngle MDecoratorWindow::desktopOrientationAngle() const
{
    const M::Orientation orientation = MDeviceProfile::instance()->orientationFromAngle(M::Angle270);
    return (orientation == M::Portrait) ? M::Angle270 : M::Angle0;
}

const QRect MDecoratorWindow::availableClientRect() const
{
    return availableRect;
}

void MDecoratorWindow::closeEvent(QCloseEvent * event )
{
    // never close the decorator!
    return event->ignore();
}

void MDecoratorWindow::addActions(QList<MAction*> new_actions)
{
    setUpdatesEnabled(false);

    navigationBar->setArrowIconVisible(false);

    QList<QAction*> oldactions = actions();

    foreach (QAction* act, oldactions)
        removeAction(act);

    foreach (MAction* act, new_actions) {
        //the signals have to be disabled because LMT using setChecked on the action and that would lead to an trigger/toggle signal
        act->blockSignals(true);
        if (act->location() == MAction::ApplicationMenuLocation)
            navigationBar->setArrowIconVisible(true);
        this->addAction(act);
        act->blockSignals(false);
    }

    setUpdatesEnabled(true);
}

void MDecoratorWindow::menuAppearing()
{
    if (menuVisible)
        return;
    menuVisible=true;
    foreach (QGraphicsItem* item, items()) {
        MApplicationMenu *menu = dynamic_cast<MApplicationMenu*>(item);
        if (menu) {
            connect(menu, SIGNAL(disappeared()), SLOT(menuDisappeared()));
            connect(this, SIGNAL(displayExited()), menu, SLOT(disappear()));
        }
    }

    QPixmap pix = QPixmap::grabWindow(winId());
    setBackgroundBrush(pix);

    setInputRegion();
}

void MDecoratorWindow::menuDisappeared()
{
    if (!menuVisible)
        return;
    menuVisible=false;
    setInputRegion();
    setBackgroundBrush(QBrush(Qt::NoBrush));
}

#include "mdecoratorwindow.moc"
