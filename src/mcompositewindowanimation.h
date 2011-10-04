/***************************************************************************
**
** Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
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

#ifndef MCOMPOSITEWINDOWANIMATOR_H
#define MCOMPOSITEWINDOWANIMATOR_H

#include <QObject>
#include <QPointer>
#include <X11/Xlib.h>

class MCompositeWindow;
class QPropertyAnimation;
class McParallelAnimation;
class QParallelAnimationGroup;
class MCompositeWindowAnimationPrivate;
class MAbstractAnimationHandler;

class MCompositeWindowAnimation: public QObject
{
    Q_OBJECT        
 public:
    enum AnimationType {
        NoAnimation = 0,
        Showing,
        Closing,
        Iconify,
        Restore,
        CrossFade,
        AnimationTotal
    };
    
    MCompositeWindowAnimation(QObject* parent = 0);
    virtual ~MCompositeWindowAnimation();

    virtual void setTargetWindow(MCompositeWindow* window);
    virtual void setEnabled(bool);
    MCompositeWindow* targetWindow() const;
    MCompositeWindow* targetWindow2() const;
    AnimationType pendingAnimation() const;
    void start();
    void finish();
    void pause();
    bool isActive();
    bool isReplaceable() const;
    void setReplaceable(bool);
    /*!
     * This animator's timeline is manually updated by hand and not running 
     * thru a timer
     */
    bool isManuallyUpdated() const;
    
    static bool hasActiveAnimation();
    
    QParallelAnimationGroup* animationGroup() const;
    QPropertyAnimation* scaleAnimation() const;
    QPropertyAnimation* positionAnimation() const;
    QPropertyAnimation* opacityAnimation() const;

    virtual void windowShown();
    virtual void windowClosed();
    virtual void windowIconified();
    virtual void windowRestored();    
    virtual bool grabAllowed() { return true; }

    void setAnimationHandler(AnimationType type, 
                             MAbstractAnimationHandler* handler);

    void crossFadeTo(MCompositeWindow *cw);    
    void ensureAnimationVisible();
    
 signals:
    /* internal signal */
    void q_finalizeState();
    void animationStopped(MCompositeWindowAnimation*);
    void animationStarted(MCompositeWindowAnimation*);

 public slots:    
    virtual void finalizeState();
    virtual void startTransition(); 

 private:
    /* internal only between priv implementation! */
    void setManuallyUpdated(bool updatemode);
    void disconnectHandler(MAbstractAnimationHandler*);
    void requestStackTop();
    
    Q_DECLARE_PRIVATE(MCompositeWindowAnimation)
    QScopedPointer<MCompositeWindowAnimationPrivate> d_ptr;

    friend class MAbstractAnimationHandler;
    friend class McParallelAnimation;
};

class MAbstractAnimationHandler
{
 public:
    virtual ~MAbstractAnimationHandler();

    virtual void windowShown();
    virtual void windowClosed();
    virtual void windowIconified();
    virtual void windowRestored();    

    MCompositeWindow* targetWindow() const;

 private:
    QPointer<MCompositeWindow> target_window;
    QPointer<MCompositeWindowAnimation> main_animator;
    friend class MCompositeWindowAnimation;
    friend class MCompositeWindowAnimationPrivate;
};

#endif // MCOMPOSITEWINDOWANIMATOR_H
