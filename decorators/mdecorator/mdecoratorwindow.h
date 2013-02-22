/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** Copyright (C) 2012 Jolla Ltd.
** Contact: Vesa Halttunen (vesa.halttunen@jollamobile.com)
**
** This file is part of mcompositor.
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation
** and appearing in the file LICENSE.LGPL included in the packaging
** of this file.
**
****************************************************************************/
#ifndef MDECORATORWINDOW_H
#define MDECORATORWINDOW_H

#include <QDeclarativeView>
#include <X11/Xlib.h>

class MDecorator;
class MDecoratorAppInterface;

class MDecoratorWindow : public QDeclarativeView
{
    Q_OBJECT
    Q_PROPERTY(bool windowVisible READ windowVisible WRITE setWindowVisible NOTIFY windowVisibleChanged)
    Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY windowTitleChanged)
    Q_PROPERTY(int orientationAngle READ orientationAngle NOTIFY orientationAngleChanged)

public:
    explicit MDecoratorWindow(QWidget *parent = 0);

    void managedWindowChanged(Qt::HANDLE window, const QString &title, int orientation, bool hung);
    bool windowVisible() const;
    QString windowTitle() const;
    int orientationAngle() const;
    void setWindowVisible(bool visible);
    void hideQueryDialog();

    Q_INVOKABLE void closeApplication();
    Q_INVOKABLE void doNotCloseApplication();

protected:
    virtual void closeEvent(QCloseEvent *event);

signals:
    void windowVisibleChanged();
    void windowTitleChanged();
    void orientationAngleChanged();

private:
    void setInputRegion();
    void setSceneSize();
    void setMDecoratorWindowProperty();

    Window managedWindow;
    MDecorator *decorator;
    MDecoratorAppInterface *appInterface;
    bool windowVisible_;
    QString windowTitle_;
    int orientationAngle_;

    Q_DISABLE_COPY(MDecoratorWindow);
};

#endif
