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

#ifndef MABSTRACTDECORATOR_H
#define MABSTRACTDECORATOR_H

#include <QObject>
#include "mabstractappinterface.h"

class MAbstractDecoratorPrivate;
class QRect;

/*!
 * MAbstractDecorator is the base class for window decorators
 */
class MAbstractDecorator: public QObject
{
    Q_OBJECT
public:
    /*!
     * Initializes MAbstractDecorator and the connections to MCompositor
     */
    MAbstractDecorator(QObject *parent = 0);
    virtual ~MAbstractDecorator();

    /*!
     * Returns the id of the window decorated by this decorator
     */
    Qt::HANDLE managedWinId();
    
    /*!
     * Informs the compositor of the available client geometry when client
     * is managed by the decorator. 
     *
     * \param rect is the geometry of the decorator area.
     */ 
    void setAvailableGeometry(const QRect& rect);

    /*!
     * Informs the compositor what was the answer to the query dialog.
     *
     * \param window managed window that the answer concerns.
     * \param yes_answer true if the answer was 'yes'.
     */ 
    void queryDialogAnswer(unsigned int window, bool yes_answer);

public slots:

    /*!
     * Interface to MRMI sockets
     */
    void RemoteSetManagedWinId(unsigned, const QRect&, const QString&,
                               unsigned, bool, bool);
    void RemoteSetOnlyStatusbar(bool mode);
    void RemoteHideQueryDialog();
    void RemotePlayFeedback(const QString &name);

protected:

     /*!
      * Pure virtual function that gets called this decorator manages a window.
      * @wmname and @orient are the title and current orientation of @window.
      * @hung indicates to show the "not responding" dialog right away.
      */
    virtual void manageEvent(Qt::HANDLE window,
                             const QString &wmname,
                             int orient,
                             bool sbonly, bool hung) = 0;

     /*!
      * Pure virtual function to set "only statusbar" mode.
      */
    virtual void setOnlyStatusbar(bool mode) = 0;

     /*!
      * Pure virtual function to hide the "not responding" query dialog.
      */
    virtual void hideQueryDialog() = 0;

     /*!
      * Pure virtual function to play a named feedback.
      */
    virtual void playFeedback(const QString &name) = 0;

private:
    
    Q_DECLARE_PRIVATE(MAbstractDecorator)
        
    MAbstractDecoratorPrivate * const d_ptr;
};

/*!
 * MAbstractAppInterface is the base class for Application Interface

   It is used to communicate to the current decorated Application.
 */
class MAbstractAppInterface: public QObject
{
    Q_OBJECT
public:
    /*!
     * Initializes MAbstractAppInterface and listens for Messages from the Application
     */
    MAbstractAppInterface(QObject *parent = 0);
    virtual ~MAbstractAppInterface() = 0;

signals:
    /*! Sends the triggered signal for the given Action to the current decorated Application*/
    void triggered(QString id, bool val);
    /*! Sends the toggled signal for the given Action to the current decorated Application*/
    void toggled(QString id, bool val);

public slots:

    /*! set the List of Actions in the Menu/ToolBar of the decorator. The window is used
        to check for the current decorated window */
    void setActions(MDecoratorIPCActionList ,uint window);

protected:

    virtual void actionsChanged(QList<MDecoratorIPCAction>, WId window) = 0;

private:

    Q_DECLARE_PRIVATE(MAbstractAppInterface)

    MAbstractAppInterfacePrivate * const d_ptr;
};

#endif //MABSTRACTDECORATOR_H
