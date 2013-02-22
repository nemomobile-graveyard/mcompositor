/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** Copyright (C) 2012 Jolla Ltd.
** Contact: Vesa Halttunen (vesa.halttunen@jollamobile.com)
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation
** and appearing in the file LICENSE.LGPL included in the packaging
** of this file.
**
****************************************************************************/

#include <QCloseEvent>
#include <QDeclarativeContext>
#include <QApplication>
#include <QDesktopWidget>
#include "mdecorator.h"
#include "mdecoratorappinterface.h"
#include "mdecoratorwindow.h"
#include <QX11Info>
#include <X11/Xatom.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>

MDecoratorWindow::MDecoratorWindow(QWidget *parent)
    : QDeclarativeView(parent),
      managedWindow(0),
      decorator(new MDecorator(this)),
      appInterface(new MDecoratorAppInterface(this)),
      windowVisible_(false),
      orientationAngle_(0)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    setSceneSize();
    setMDecoratorWindowProperty();
    setInputRegion();
    setResizeMode(QDeclarativeView::SizeRootObjectToView);
    viewport()->setAutoFillBackground(false);
    rootContext()->setContextProperty("initialSize", QApplication::desktop()->screenGeometry(this).size());
    rootContext()->setContextProperty("decoratorWindow", this);
    setSource(QUrl("qrc:/qml/main.qml"));
    show();
}

void MDecoratorWindow::managedWindowChanged(Qt::HANDLE window, const QString &title, int orientation, bool hung)
{
    appInterface->setManagedWindow(window);
    managedWindow = window;

    if (windowTitle_ != title) {
        windowTitle_ = title;
        emit windowTitleChanged();
    }

    if (orientationAngle_ != orientation) {
        orientationAngle_ = orientation;
        emit orientationAngleChanged();
    }

    setWindowVisible(hung);
}

void MDecoratorWindow::setInputRegion()
{
    Display *dpy = QX11Info::display();
    XRectangle rect;
    rect.x = 0;
    rect.y = 0;
    if (windowVisible_) {
        rect.width = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
        rect.height = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;
    } else {
        rect.width = 0;
        rect.height = 0;
    }
    XserverRegion shapeRegion = XFixesCreateRegion(dpy, &rect, 1);
    XFixesSetWindowShapeRegion(dpy, winId(), ShapeInput, 0, 0, shapeRegion);
    XFixesDestroyRegion(dpy, shapeRegion);
}

void MDecoratorWindow::setSceneSize()
{
    Display *dpy = QX11Info::display();
    int xres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
    int yres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;
    setMinimumSize(xres, yres);
    setMaximumSize(xres, yres);
}

void MDecoratorWindow::setMDecoratorWindowProperty()
{
    long on = 1;

    XChangeProperty(QX11Info::display(), winId(), XInternAtom(QX11Info::display(), "_MEEGOTOUCH_DECORATOR_WINDOW", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&on, 1);
}

void MDecoratorWindow::closeEvent(QCloseEvent *event)
{
    return event->ignore();
}

bool MDecoratorWindow::windowVisible() const
{
    return windowVisible_;
}

void MDecoratorWindow::setWindowVisible(bool visible)
{
    if (windowVisible_ != visible) {
        windowVisible_ = visible;
        setInputRegion();
        emit windowVisibleChanged();
    }
}

QString MDecoratorWindow::windowTitle() const
{
    return windowTitle_;
}

int MDecoratorWindow::orientationAngle() const
{
    return orientationAngle_;
}

void MDecoratorWindow::closeApplication()
{
    decorator->queryDialogAnswer(managedWindow, true);
    hideQueryDialog();
}

void MDecoratorWindow::doNotCloseApplication()
{
    decorator->queryDialogAnswer(managedWindow, false);
    hideQueryDialog();
}

void MDecoratorWindow::hideQueryDialog()
{
    setWindowVisible(false);
}
