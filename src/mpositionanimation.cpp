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
#include <mcompositewindowshadereffect.h>
#include <mstatusbartexture.h>

#include "mpositionanimation.h"

static QRectF screen;

MPositionAnimation::MPositionAnimation(QObject* parent)
    :MCompositeWindowAnimation(parent)
{
    setReplaceable(false);
    // only use the position animation
    animationGroup()->removeAnimation(scaleAnimation());
    animationGroup()->removeAnimation(opacityAnimation());
    position = positionAnimation();
    
    if (screen.isEmpty())
        screen = QApplication::desktop()->availableGeometry();  
}

MPositionAnimation::~MPositionAnimation()
{
    // don't delete externally referenced animation objects
    for (int i = 0; i < animationGroup()->animationCount(); ++i)
        if (animationGroup()->animationAt(i) != position)
            animationGroup()->takeAnimation(i);
}

void MPositionAnimation::setEnabled(bool enabled)
{
    if (enabled && (animationGroup()->indexOfAnimation(position) == -1))
        animationGroup()->addAnimation(position);
    else if (!enabled) {
        animationGroup()->stop();
        animationGroup()->removeAnimation(position);
    }
}

MSheetAnimation::MSheetAnimation(QObject* parent)
    :MPositionAnimation(parent)
{
    // From the UX specs
    positionAnimation()->setDuration(350);
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

class MStatusBarCrop: public MCompositeWindowShaderEffect
{
    Q_OBJECT
 public:
    MStatusBarCrop(QObject* parent)
        :MCompositeWindowShaderEffect(parent),
         appwindow(0),
         portrait(false)
    {
         p_transform.rotate(90);
         p_transform.translate(0, -screen.height());
         p_transform = p_transform.inverted();
    }
    
    virtual void drawTexture(const QTransform &transform,
                     const QRectF &drawRect, qreal opacity)
    {
        const MStatusBarTexture *sbtex = MStatusBarTexture::instance();
        QRectF draw_rect(drawRect);
        if (appwindow && appwindow->propertyCache()
            && !appwindow->propertyCache()->statusbarGeometry().isEmpty()) {
            // subtract statusbar
            if (portrait)
                draw_rect.setLeft(sbtex->portraitRect().height());
            else
                draw_rect.setTop(sbtex->landscapeRect().height());
        }
        
        // original texture with cropped statusbar
        glBindTexture(GL_TEXTURE_2D, texture());
        drawSource(transform, draw_rect, opacity, true); 

        // draw status bar texture
        if (!portrait) {
            glBindTexture(GL_TEXTURE_2D, sbtex->landscapeTexture());
            drawSource(QTransform(),
                       sbtex->landscapeRect(), 1.0);
        } else {
            glBindTexture(GL_TEXTURE_2D, sbtex->portraitTexture());
            drawSource(p_transform,
                       sbtex->portraitRect(), 1.0);
        }
    }

    void setAppWindow(MCompositeWindow* a)
    {
        appwindow = a;
    }

    void setPortrait(bool p)
    {
        portrait = p;
    }

private:
    MCompositeWindow* appwindow;
    QTransform p_transform;
    bool portrait;
};

MChainedAnimation::MChainedAnimation(QObject* parent)
    :MPositionAnimation(parent)
{
    // UX subview specs
    positionAnimation()->setDuration(500);
    positionAnimation()->setEasingCurve(QEasingCurve::InOutExpo);

    invoker_pos = new QPropertyAnimation(this);
    invoker_pos->setPropertyName("pos");
    invoker_pos->setEasingCurve(QEasingCurve::InOutExpo);
    invoker_pos->setDuration(500);
    animationGroup()->addAnimation(invoker_pos);
    
    connect(animationGroup(), SIGNAL(finished()), SLOT(endAnimation()));
    cropper = new MStatusBarCrop(this);
}

void MChainedAnimation::windowShown()
{
    setEnabled(true);

    MStatusBarTexture::instance()->updatePixmap();
    // sb geometry is shared by targetwindow and invokerwindow
    cropper->setAppWindow(targetWindow());
    cropper->installEffect(targetWindow());
    cropper->installEffect(invokerWindow());
    targetWindow()->setOpacity(1.0);
    invokerWindow()->setVisible(true);
    invoker_pos->setTargetObject(invokerWindow());
    
    bool portrait = targetWindow()->propertyCache()->orientationAngle() % 180;
    cropper->setPortrait(portrait);
    if (portrait) {
        positionAnimation()->setStartValue(screen.translated(0,-screen.height())
                                           .topLeft());
        positionAnimation()->setEndValue(QPointF(0,0));
        invoker_pos->setStartValue(QPointF(0,0));
        invoker_pos->setEndValue(screen.bottomLeft());
    } else {
        positionAnimation()->setStartValue(screen.topRight());
        positionAnimation()->setEndValue(QPointF(0,0));
        invoker_pos->setStartValue(QPointF(0,0));
        invoker_pos->setEndValue(screen.translated(0,-screen.width())
                                 .topLeft());        
    }

    animationGroup()->setDirection(QAbstractAnimation::Forward);
    start();
}

void MChainedAnimation::windowClosed()
{
    setEnabled(true);
    
    MStatusBarTexture::instance()->updatePixmap();
    cropper->setAppWindow(targetWindow());
    cropper->installEffect(targetWindow());
    cropper->installEffect(invokerWindow());
    invokerWindow()->setVisible(true);
    invoker_pos->setTargetObject(invokerWindow());
    
    animationGroup()->setDirection(QAbstractAnimation::Backward);
    animationGroup()->start();    
}

MCompositeWindow* MChainedAnimation::invokerWindow()
{
    if (targetWindow()->propertyCache()->invokedBy() != None)
        return MCompositeWindow::compositeWindow(targetWindow()->propertyCache()->invokedBy());
    return 0;
}

void MChainedAnimation::endAnimation()
{
    cropper->removeEffect(targetWindow());
    cropper->removeEffect(invokerWindow());
    MStatusBarTexture::instance()->untrackDamages();
}

#include "mpositionanimation.moc"