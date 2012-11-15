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

#ifndef DUIDECORATORFRAME_H
#define DUIDECORATORFRAME_H

#include <QObject>
#include <QRect>

class MCompositeWindow;
class MRmiClient;
class QRect;

/*!
 * MDecoratorFrame is a singleton class that represents the decorator process
 * which draws the decorations for non-DirectUI applications.
 * This class handles the communication as well to the decorator.
 */
class MDecoratorFrame: public QObject
{
    Q_OBJECT
public:
    /*!
     * Creates the singleton
     */
    MDecoratorFrame(QObject *object = 0);

    /*!
     * Singleton accessor
     */
    static MDecoratorFrame *instance() { return d; }

    /*!
     * Retuns the window id of the managed window.
     */
    Qt::HANDLE managedWindow() const;
    MCompositeWindow *managedClient() const { return client; }

    /*!
     * Returns the window id of the decorator window.
     */
    Qt::HANDLE winId() const;

    /*!
     * Hides the decorator QGraphicsItem.
     */
    void hide();

    /*!
     * Shows the decorator QGraphicsItem.
     */
    void show();

    /*!
     * Sets the managed window.
     */
    void setManagedWindow(MCompositeWindow *cw,
                          bool no_resize = false,
                          bool only_statusbar = false,
                          bool show_dialog = false);

    /*!
     * Manage @cw and show the query dialog.
     */
    void showQueryDialog(MCompositeWindow *cw, bool only_statusbar = false);

    /*!
     * Hide the query dialog.
     */
    void hideQueryDialog();

    /*!
     * Play a named feedback.
     */
    void playFeedback(const QString &name);

    /*!
     * Sets the "only statusbar" mode.
     */
    void setOnlyStatusbar(bool mode);

    /*!
     * Sets the decorator window and maps that window if it is unmapped.
     */
    void setDecoratorWindow(Qt::HANDLE window);

    void setDecoratorItem(MCompositeWindow *window);

    MCompositeWindow *decoratorItem() const;
    const QRect &availableRect() const { return available_rect; }

private slots:
    void decoratorRectChanged(const QRect& r);
    void queryDialogAnswer(unsigned window, bool killit);
    void destroyDecorator();
    void destroyClient();

private:
    void sendManagedWindowId(bool show_dialog = false);
    static MDecoratorFrame *d;

    MCompositeWindow *client;
    Qt::HANDLE decorator_window;
    MCompositeWindow *decorator_item;
    MRmiClient *remote_decorator;
    int top_offset;
    int sent_orientation;
    bool sent_show_dialog;
    bool no_resize, only_statusbar;
    QRect available_rect;
};

#endif // DUIDECORATORFRAME_H
