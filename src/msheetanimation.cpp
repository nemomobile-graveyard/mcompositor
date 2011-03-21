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

#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QPointF>
#include <QtDebug>
#include <mcompositewindow.h>

#include "msheetanimation.h"

static QRectF screen;

MSheetAnimation::MSheetAnimation(QObject* parent)
    :MCompositeWindowAnimation(parent)
{
    // only use the position animation
    animationGroup()->removeAnimation(scaleAnimation());
    animationGroup()->removeAnimation(opacityAnimation());
    
    if (screen.isEmpty())
        screen = QApplication::desktop()->availableGeometry();  
    // From the UX specs
    positionAnimation()->setDuration(350);
    position = positionAnimation();
}

MSheetAnimation::~MSheetAnimation()
{
    // don't delete externally referenced animation objects
    for (int i = 0; i < animationGroup()->animationCount(); ++i)
        if (animationGroup()->animationAt(i) != position)
            animationGroup()->takeAnimation(i);
}

void MSheetAnimation::windowShown()
{
    setEnabled(true);
    targetWindow()->setOpacity(1.0);

    bool portrait = targetWindow()->propertyCache()->orientationAngle() % 180;
    positionAnimation()->setEasingCurve(QEasingCurve::OutExpo);
    
    if (portrait) {    
        positionAnimation()->setStartValue(screen.topRight());
        positionAnimation()->setEndValue(QPointF(0,0));
    } else {
        positionAnimation()->setStartValue(screen.bottomLeft());
        positionAnimation()->setEndValue(QPointF(0,0));
    }
    
    animationGroup()->setDirection(QAbstractAnimation::Forward);
    start();
}

void MSheetAnimation::windowClosed()
{
    setEnabled(true);
    positionAnimation()->setEasingCurve(QEasingCurve::InOutExpo);
    animationGroup()->setDirection(QAbstractAnimation::Backward);
    animationGroup()->start();
    
}

void MSheetAnimation::setEnabled(bool enabled)
{
    if (enabled && (animationGroup()->indexOfAnimation(position) == -1))
        animationGroup()->addAnimation(position);
    else if (!enabled) {
        animationGroup()->stop();
        animationGroup()->removeAnimation(position);
    }
}
