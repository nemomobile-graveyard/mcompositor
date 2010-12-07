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
#include <MAction>
#include <MLabel>
#include <QGraphicsLinearLayout>

#include <QApplication>
#include <QDesktopWidget>
#include <QX11Info>
#include <QGLFormat>
#include <QGLWidget>

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

class MDecorator: public MAbstractDecorator
{
    Q_OBJECT
public:
    MDecorator(MDecoratorWindow *p)
        : MAbstractDecorator(p),
        decorwindow(p)
    {
    }

    ~MDecorator() {
    }

    virtual void manageEvent(Qt::HANDLE window)
    {
        XTextProperty p;
        QString title;

        if(XGetWMName(QX11Info::display(), window, &p)) {
            if (p.value) {
                title = (char*) p.value;
                XFree(p.value);
            }
        }
        decorwindow->managedWindowChanged(window);
        decorwindow->setInputRegion();
        setAvailableGeometry(decorwindow->availableClientRect());
        decorwindow->setWindowTitle(title);
    }

protected:
    virtual void activateEvent() {
    }

    virtual void showQueryDialog(bool visible) {
        decorwindow->showQueryDialog(visible);
    }

    virtual void setAutoRotation(bool mode)
    {
        Q_UNUSED(mode)
        // we follow the orientation of the topmost app
    }

    virtual void setOnlyStatusbar(bool mode) 
    {
        decorwindow->setOnlyStatusbar(mode);
        decorwindow->setInputRegion();
        setAvailableGeometry(decorwindow->availableClientRect());
    }

private:

    MDecoratorWindow *decorwindow;
};

class MDecoratorAppInterface : public MAbstractAppInterface
{
    Q_OBJECT

public:
    MDecoratorAppInterface(MDecoratorWindow *p)
        :MAbstractAppInterface(p)
        ,decorwindow(p)
    {
    }

    virtual void appMenuChanged(QList<IPCAction> newMenu)
    {
        qCritical()<<__PRETTY_FUNCTION__<<"new Actions:"<<newMenu.count();
        QList<MAction*> menu;
        actionHash.clear();

        foreach(IPCAction act, newMenu) {
            MAction* mact = createMAction(act);
            connect(mact,SIGNAL(triggered(bool)),SLOT(actionTriggered(bool)));
            connect(mact,SIGNAL(toggled(bool)),SLOT(actionToggled(bool)));
            menu.append(mact);
        }
        decorwindow->setApplicationMenu(menu);
    }

public slots:
    void actionTriggered(bool val)
    {
        qCritical()<<"triggered";
        if(!sender() && !qobject_cast<MAction*>(sender()))
            return;

        MAction* act = static_cast<MAction*>(sender());
        if(actionHash.contains(act))
            triggered(actionHash[act],val);
    }

    void actionToggled(bool val)
    {
        qCritical()<<"toggled";
        if(!sender() && !qobject_cast<MAction*>(sender()))
            return;

        MAction* act = static_cast<MAction*>(sender());
        if(actionHash.contains(act))
            triggered(actionHash[act], val);
    }

private:

    MAction* createMAction(const IPCAction& act)
    {
        qCritical()<<"creating Action: checkable"<<act.isCheckable()<<"checked"<<act.isChecked();
        MAction* mact = new MAction(decorwindow);
        mact->setText(act.text());
        mact->setCheckable(act.isCheckable());
        mact->setChecked(act.isChecked());
        mact->setLocation(MAction::ApplicationMenuLocation);
        actionHash[mact] = act;
        return mact;
    }
    QHash<MAction*,IPCAction> actionHash;

    MDecoratorWindow *decorwindow;
};

#if 0
static QRect windowRectFromGraphicsItem(const QGraphicsView &view,
                                        const QGraphicsItem &item)
{
    return view.mapFromScene(
               item.mapToScene(
                   item.boundingRect()
               )
           ).boundingRect();
}
#endif

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
    locale.addTranslationPath(TRANSLATION_INSTALLDIR);
    locale.installTrCatalog("recovery");
    locale.setDefault(locale);

    onlyStatusbarAtom = XInternAtom(QX11Info::display(),
                                    "_MDECORATOR_ONLY_STATUSBAR", False);
    managedWindowAtom = XInternAtom(QX11Info::display(),
                                    "_MDECORATOR_MANAGED_WINDOW", False);

    foreach(QGraphicsItem* item, items()) {
        if(!homeButtonPanel) {
            homeButtonPanel = dynamic_cast<MHomeButtonPanel*>(item);
            if(homeButtonPanel)
                continue;
        }
        if(!escapeButtonPanel) {
            escapeButtonPanel = dynamic_cast<MEscapeButtonPanel*>(item);
            if(escapeButtonPanel)
                continue;
        }
        if(!navigationBar) {
            navigationBar = dynamic_cast<MNavigationBar*>(item);
            if(navigationBar)
                continue;
        }
        if(!statusBar) {
            statusBar = dynamic_cast<MStatusBar*>(item);
            if(statusBar)
                continue;
        }
    }


    if(!homeButtonPanel || !navigationBar ||!statusBar) {
        qCritical()<<"Meego elements not found"<<homeButtonPanel<<navigationBar<<statusBar;
    }

    homeButtonPanel = new MHomeButtonPanel();
    connect(homeButtonPanel, SIGNAL(buttonClicked()), this,
            SIGNAL(homeClicked()));
    if(escapeButtonPanel)
        connect(escapeButtonPanel, SIGNAL(buttonClicked()), this,
                SIGNAL(escapeClicked()));

    setComponentsDisplayMode(NavigationBar, MApplicationPageModel::Show);

    connect(navigationBar,SIGNAL(viewmenuTriggered()),SLOT(menuAppearing()));

    /*sceneManager()->appearSceneWindowNow(statusBar);
    sceneManager()->appearSceneWindowNow(menu);*/
    setOnlyStatusbar(false);
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
    setFocusPolicy(Qt::NoFocus);
    setSceneSize();
    setMDecoratorWindowProperty();

    setInputRegion();
    setProperty("followsCurrentApplicationWindowOrientation", true);
}

void MDecoratorWindow::yesButtonClicked()
{
    d->queryDialogAnswer(managed_window, true);
    showQueryDialog(false);
}

void MDecoratorWindow::noButtonClicked()
{
    d->queryDialogAnswer(managed_window, false);
    showQueryDialog(false);
}

void MDecoratorWindow::managedWindowChanged(Qt::HANDLE w)
{
    app->appMenuChanged(QList<IPCAction>());
    if (w != managed_window && messageBox)
        showQueryDialog(false);
    managed_window = w;
}

void MDecoratorWindow::setWindowTitle(const QString& title)
{
    navigationBar->setViewMenuDescription(title);
}

MDecoratorWindow::~MDecoratorWindow()
{
}

bool MDecoratorWindow::x11Event(XEvent *e)
{
    Atom actual;
    int format, result;
    unsigned long n, left;
    unsigned char *data = 0;
    if (e->type == PropertyNotify
        && ((XPropertyEvent*)e)->atom == onlyStatusbarAtom) {
        result = XGetWindowProperty(QX11Info::display(), winId(),
                                    onlyStatusbarAtom, 0, 1, False,
                                    XA_CARDINAL, &actual, &format,
                                    &n, &left, &data);
        if (result == Success && data) {
            bool val = *((long*)data);
            if (val != only_statusbar)
                d->RemoteSetOnlyStatusbar(val);
        }
        if (data)
            XFree(data);
        return true;
    } else if (e->type == PropertyNotify
               && ((XPropertyEvent*)e)->atom == managedWindowAtom) {
        result = XGetWindowProperty(QX11Info::display(), winId(),
                                    managedWindowAtom, 0, 1, False,
                                    XA_WINDOW, &actual, &format,
                                    &n, &left, &data);
        if (result == Success && data)
            d->RemoteSetManagedWinId(*((long*)data));
        if (data)
            XFree(data);
        return true;
    }
    return false;
}

void MDecoratorWindow::showQueryDialog(bool visible)
{
    if (visible && !messageBox) {
        QString name;

        XClassHint cls = {0, 0};
        XGetClassHint(QX11Info::display(), managed_window, &cls);
        if (cls.res_name) {
            name = QString(cls.res_name);
            MDesktopEntry de(QString("/usr/share/applications/")
                             + name + ".desktop");
            if (de.isValid() && !de.name().isEmpty())
                name = de.name();
            XFree(cls.res_name);
        } else
            name.sprintf("window 0x%lx", managed_window);

        if (cls.res_class)
            XFree(cls.res_class);

        XSetTransientForHint(QX11Info::display(), winId(), managed_window);
        requested_only_statusbar = only_statusbar;
        setOnlyStatusbar(true, true);
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
        MButtonModel *yes = messageBox->addButton(qtTrId("qtn_comm_command_yes"),
                                                  M::AcceptRole);
        MButtonModel *no = messageBox->addButton(qtTrId("qtn_comm_command_no"),
                                                 M::RejectRole);
        connect(yes, SIGNAL(clicked()), this, SLOT(yesButtonClicked()));
        connect(no, SIGNAL(clicked()), this, SLOT(noButtonClicked()));
        sceneManager()->appearSceneWindowNow(messageBox);
    } else if (!visible && messageBox) {
        XSetTransientForHint(QX11Info::display(), winId(), None);
        sceneManager()->disappearSceneWindowNow(messageBox);
        delete messageBox;
        messageBox = 0;
        setOnlyStatusbar(requested_only_statusbar);
    }
    setInputRegion();
    update();
}

void MDecoratorWindow::setOnlyStatusbar(bool mode, bool temporary)
{
    if (mode) {
        sceneManager()->disappearSceneWindowNow(navigationBar);
        sceneManager()->disappearSceneWindowNow(homeButtonPanel);
        if (escapeButtonPanel)
            sceneManager()->disappearSceneWindowNow(escapeButtonPanel);
    } else if (!messageBox) {
        sceneManager()->appearSceneWindowNow(navigationBar);
        sceneManager()->appearSceneWindowNow(homeButtonPanel);
        if (escapeButtonPanel)
            sceneManager()->appearSceneWindowNow(escapeButtonPanel);
    }
    if (!temporary)
        requested_only_statusbar = mode;
    only_statusbar = mode;
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
    } else {
        // Decoration includes the status bar, and possibly other elements.
        region = statusBar->geometry().toRect();
        if (!only_statusbar) {
            region += navigationBar->geometry().toRect();
            region += homeButtonPanel->geometry().toRect();
            if (escapeButtonPanel)
                region += escapeButtonPanel->geometry().toRect();
        }

        // The coordinates we receive from libmeegotouch are rotated
        // by @angle.  Map @retion back to screen coordinates.
        int angle = sceneManager()->orientationAngle();
        if (angle != 0) {
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
    }

    // Set our input and bounding shape to @region if changed.
    if (prev_region != region) {
        prev_region = region;

        // Convert @region to @xrects.
        const QVector<QRect> rects = region.rects();
        int nxrects = rects.count();
        XRectangle *xrects = new XRectangle[nxrects];
        for (int i = 0; i < nxrects; ++i) {
            xrects[i].x = rects[i].x();
            xrects[i].y = rects[i].y();
            xrects[i].width = rects[i].width();
            xrects[i].height = rects[i].height();
        }

        Display *dpy = QX11Info::display();
        XserverRegion shapeRegion = XFixesCreateRegion(dpy, xrects, nxrects);
        XFixesSetWindowShapeRegion(dpy, winId(), ShapeInput,
                                   0, 0, shapeRegion);
        XFixesSetWindowShapeRegion(dpy, winId(), ShapeBounding,
                                   0, 0, shapeRegion);

        XFixesDestroyRegion(dpy, shapeRegion);
        delete[] xrects;
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
                    XInternAtom(QX11Info::display(), "_MEEGOTOUCH_DECORATOR_WINDOW", False),
                    XA_CARDINAL,
                    32, PropModeReplace,
                    (unsigned char *) &on, 1);
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

void MDecoratorWindow::setApplicationMenu(QList<MAction*> new_actions)
{
    qCritical()<<__PRETTY_FUNCTION__;

    QList<QAction*> oldactions = actions();

    //first add the new actions, otherwise the bar will disappear because there are no actions
    foreach(MAction* act, new_actions) {
        this->addAction(act);
    }

    //clear actions first;
    foreach(QAction* act, oldactions)
        removeAction(act);

    if(new_actions.isEmpty())
        navigationBar->setArrowIconVisible(false);
    else
        navigationBar->setArrowIconVisible(true);
}

void MDecoratorWindow::menuAppearing()
{
    qCritical()<<__PRETTY_FUNCTION__;
    if(menuVisible)
        return;
    menuVisible=true;
    foreach(QGraphicsItem* item, items()) {
        MApplicationMenu *menu = dynamic_cast<MApplicationMenu*>(item);
        if(menu) {
            connect(menu,SIGNAL(appeared()),SLOT(menuAppeared()));
            connect(menu,SIGNAL(disappeared()),SLOT(menuDisappeared()));
        }
    }

    QPixmap pix = QPixmap::grabWindow(winId());
    setBackgroundBrush(pix);

    setInputRegion();
}

void MDecoratorWindow::menuAppeared()
{
    //force a repaint, otherwise not the whole menu is displayed
    repaint();
}

void MDecoratorWindow::menuDisappeared()
{
    qCritical()<<__PRETTY_FUNCTION__;
    if(!menuVisible)
        return;
    menuVisible=false;
    setInputRegion();
    setBackgroundBrush(QBrush(Qt::NoBrush));
}

#include "mdecoratorwindow.moc"
