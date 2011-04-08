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
#include <X11/Xlib.h>

class MCompositeWindow;
class QPropertyAnimation;
class McParallelAnimation;
class QParallelAnimationGroup;
class MCompositeWindowAnimationPrivate;

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
        CrossFade
    };
    
    MCompositeWindowAnimation(QObject* parent = 0);
    virtual ~MCompositeWindowAnimation();

    virtual void setTargetWindow(MCompositeWindow* window); 
    MCompositeWindow* targetWindow() const;
    MCompositeWindow* targetWindow2() const;
    AnimationType pendingAnimation() const;
    void start();
    virtual void finish();
    void pause();
    bool isActive();
    
    QParallelAnimationGroup* animationGroup() const;
    QPropertyAnimation* scaleAnimation() const;
    QPropertyAnimation* positionAnimation() const;
    QPropertyAnimation* opacityAnimation() const;

    virtual void windowShown();
    virtual void windowClosed();
    virtual void windowIconified();
    virtual void windowRestored();    
    void crossFadeTo(MCompositeWindow *cw);    
    void deferAnimation(AnimationType type);
    void ensureAnimationVisible();
    
 signals:
    /* internal signal */
    void q_finalizeState();

 public slots:    
    virtual void finalizeState();
    virtual void startTransition();

 private:
    Q_DECLARE_PRIVATE(MCompositeWindowAnimation)
    QScopedPointer<MCompositeWindowAnimationPrivate> d_ptr;
};

#endif // MCOMPOSITEWINDOWANIMATOR_H
