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

#ifndef MPOSITIONANIMATION_H
#define MPOSITIONANIMATION_H

class QPropertyAnimation;
class MStatusBarCrop;

#include <mcompositewindowanimation.h>

class MPositionAnimation: public MCompositeWindowAnimation
{
    Q_OBJECT
 public:
    MPositionAnimation(QObject* parent = 0);
    ~MPositionAnimation();
    void setEnabled(bool enabled);

 private:
    QPropertyAnimation* position;
};

class MSheetAnimation: public MPositionAnimation
{
    Q_OBJECT
 public:
    MSheetAnimation(QObject* parent = 0);

    virtual void windowShown(); 
    virtual void windowClosed();
};

class MChainedAnimation: public MPositionAnimation
{
    Q_OBJECT
 public:
    MChainedAnimation(QObject* parent = 0);

    virtual void windowShown(); 
    virtual void windowClosed();

 private slots:
    void endAnimation();

 private:
    MCompositeWindow* invokerWindow();
    QPropertyAnimation* invoker_pos;
    MStatusBarCrop* cropper;
};

#endif
