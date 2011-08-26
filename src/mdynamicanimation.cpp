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
#include <mcompositewindow.h>
#include <mcompositewindowshadereffect.h>
#include <mstatusbartexture.h>
#include <mcompositemanager.h>
#include <mdevicestate.h>

#include "mcompositemanager_p.h"
#include "mdynamicanimation.h"

static QRectF screen;

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
    
    void drawTexture(const QTransform &transform,
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
    QPointer<MCompositeWindow> appwindow;
    QTransform p_transform;
    bool portrait;
};

/*!
 * Dynamic animators are window animators that can have their internal
 * animation objects switched and shared between windows
 */
MDynamicAnimation::MDynamicAnimation(QObject* parent)
    :MCompositeWindowAnimation(parent)
{
    setReplaceable(false);    
    if (screen.isEmpty())
        screen = QApplication::desktop()->availableGeometry();  
}

static void removeExternalAnimations(QAnimationGroup* group, 
                                     const AnimVector& skipvec)
{
    int i = 0;
    while (i < group->animationCount()) {
        QAbstractAnimation* a = group->animationAt(i);
        if (a && !skipvec.contains(a)) {
            group->removeAnimation(a);
            continue;
        }
        ++i;
    }
}
/*!
  Disable an animation object by removing it from this animator's control
  and at the same returning ownership to this object
 */
void MDynamicAnimation::disableAnimation(QAbstractAnimation *animation )
{
    animationGroup()->removeAnimation(animation);
    animation->setParent(this);
}

AnimVector& MDynamicAnimation::activeAnimations()
{
    return animvec;
}

MDynamicAnimation::~MDynamicAnimation()
{
    // Prevent auto deleting externally referenced animation objects
    removeExternalAnimations(animationGroup(), activeAnimations());
}

void MDynamicAnimation::setEnabled(bool enabled)
{
    if (enabled) {
        AnimVector s = activeAnimations();
        for (int i = 0; i < s.size(); ++i) {
            if (animationGroup()->indexOfAnimation(s[i]) == -1) 
                animationGroup()->addAnimation(s[i]);
        }
        // remove external animation objects we don't own
        removeExternalAnimations(animationGroup(), activeAnimations());

    } else if (!enabled) {
        AnimVector s = activeAnimations();
        for (int i = 0; i < s.size(); ++i) {
            if(animationGroup()->indexOfAnimation(s[i]) != -1)
                disableAnimation(s[i]);
        }
    }
}

MSheetAnimation::MSheetAnimation(QObject* parent)
    :MDynamicAnimation(parent)
{
    // only use the position animation
    disableAnimation(scaleAnimation());
    disableAnimation(opacityAnimation());

    // From the UX specs
    const MCompositeManager *mc = static_cast<MCompositeManager*>(qApp);
    int duration = mc->configInt("sheet-anim-duration", 350);
    positionAnimation()->setDuration(duration);
    activeAnimations().append(positionAnimation());
    cropper = new MStatusBarCrop(this);
    connect(animationGroup(), SIGNAL(finished()), SLOT(endAnimation()));
}

void MSheetAnimation::windowShown()
{
    if (!targetWindow())
        return;
    setEnabled(true);
    targetWindow()->setOpacity(1.0);

    initializePositionAnimation();
    positionAnimation()->setEasingCurve(QEasingCurve::OutExpo);

    if (!targetWindow()->propertyCache()->statusbarGeometry().isEmpty()) {
        MStatusBarTexture::instance()->updatePixmap();
        cropper->setAppWindow(targetWindow());
        cropper->installEffect(targetWindow());
        bool p = targetWindow()->propertyCache()->orientationAngle() % 180;
        cropper->setPortrait(p);
    }
    
    animationGroup()->setDirection(QAbstractAnimation::Forward);
    start();
}

void MSheetAnimation::windowClosed()
{
    if (!targetWindow())
        return;
    setEnabled(true);
    initializePositionAnimation();
    positionAnimation()->setEasingCurve(QEasingCurve::InOutExpo);

    if (!targetWindow()->propertyCache()->statusbarGeometry().isEmpty()) {
        MStatusBarTexture::instance()->updatePixmap();
        cropper->setAppWindow(targetWindow());
        cropper->installEffect(targetWindow());
        bool p = targetWindow()->propertyCache()->orientationAngle() % 180;
        cropper->setPortrait(p);
    }
    
    animationGroup()->setDirection(QAbstractAnimation::Backward);
    targetWindow()->setVisible(true);
    if (targetWindow()->behind())
       targetWindow()->behind()->setVisible(true);
    animationGroup()->start();    
}

void MSheetAnimation::endAnimation()
{
    cropper->removeEffect(targetWindow());
    MStatusBarTexture::instance()->untrackDamages();
}

void MSheetAnimation::initializePositionAnimation()
{
    const bool portrait = targetWindow()->propertyCache()->orientationAngle() % 180;
    positionAnimation()->setStartValue(portrait ? screen.topRight() : screen.bottomLeft());
    positionAnimation()->setEndValue(QPointF(0,0));
}

MChainedAnimation::MChainedAnimation(QObject* parent)
    :MDynamicAnimation(parent)
{    
    // only use the position animation
    disableAnimation(scaleAnimation());
    disableAnimation(opacityAnimation());

    // UX subview specs    
    const MCompositeManager *mc = static_cast<MCompositeManager*>(qApp);
    int duration = mc->configInt("chained-anim-duration", 500);
    positionAnimation()->setDuration(duration);
    positionAnimation()->setEasingCurve(QEasingCurve::InOutExpo);

    invoker_pos = new QPropertyAnimation(this);
    invoker_pos->setPropertyName("pos");
    invoker_pos->setEasingCurve(QEasingCurve::InOutExpo);
    invoker_pos->setDuration(duration);
    animationGroup()->addAnimation(invoker_pos);
    
    connect(animationGroup(), SIGNAL(finished()), SLOT(endAnimation()));
    cropper = new MStatusBarCrop(this);

    activeAnimations().append(positionAnimation());
    activeAnimations().append(invoker_pos);    
}

void MChainedAnimation::windowShown()
{
    if (!targetWindow() || !invokerWindow()) {
        if (invokerWindow())
            invokerWindow()->setVisible(true);
        if (targetWindow())
            targetWindow()->setVisible(true);
        return;
    }
    setEnabled(true);

    MStatusBarTexture::instance()->updatePixmap();
    // sb geometry is shared by targetwindow and invokerwindow
    cropper->setAppWindow(targetWindow());
    if (!targetWindow()->propertyCache()->statusbarGeometry().isEmpty()) {
        cropper->installEffect(targetWindow());
        cropper->installEffect(invokerWindow());
    }
    targetWindow()->setOpacity(1.0);
    invokerWindow()->setVisible(true);
    invoker_pos->setTargetObject(invokerWindow());
    
    // use invokerWindow() orientation to work around NB#279547's cause
    bool portrait = invokerWindow()->propertyCache()->orientationAngle() % 180;
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
    if (!targetWindow() || !invokerWindow())
        return;
    setEnabled(true);
    
    MStatusBarTexture::instance()->updatePixmap();
    cropper->setAppWindow(targetWindow());
    
    if (!targetWindow()->propertyCache()->statusbarGeometry().isEmpty()) {
        cropper->installEffect(targetWindow());
        cropper->installEffect(invokerWindow());
    }
    invokerWindow()->setVisible(true);
    invoker_pos->setTargetObject(invokerWindow());
    
    animationGroup()->setDirection(QAbstractAnimation::Backward);
    animationGroup()->start();    
}

MCompositeWindow* MChainedAnimation::invokerWindow()
{
    if (targetWindow() && targetWindow()->propertyCache()->invokedBy() != None)
        return MCompositeWindow::compositeWindow(
                         targetWindow()->propertyCache()->invokedBy());
    return 0;
}

void MChainedAnimation::endAnimation()
{
    cropper->removeEffect(targetWindow());
    cropper->removeEffect(invokerWindow());
    MStatusBarTexture::instance()->untrackDamages();
}

MCallUiAnimation::MCallUiAnimation(QObject* parent)
    :MDynamicAnimation(parent),
     call_mode(MCallUiAnimation::NoCall)
{
    connect(animationGroup(), SIGNAL(finished()), SLOT(endAnimation()));
    cropper = new MStatusBarCrop(this);

    // UX call-ui specs
    const MCompositeManager *mc = static_cast<MCompositeManager*>(qApp);
    int duration = mc->configInt("callui-anim-duration", 400);
    currentwin_pos = new QPropertyAnimation(this);
    currentwin_pos->setPropertyName("pos");
    currentwin_pos->setEasingCurve(QEasingCurve::InOutCubic);
    currentwin_pos->setDuration(duration);

    currentwin_scale = new QPropertyAnimation(this);
    currentwin_scale->setPropertyName("scale");
    currentwin_scale->setStartValue(1.0);
    currentwin_scale->setEndValue(0.6);
    currentwin_scale->setEasingCurve(QEasingCurve::InOutCubic);
    currentwin_scale->setDuration(duration);
    
    currentwin_opac = new QPropertyAnimation(this);
    currentwin_opac->setPropertyName("opacity");
    currentwin_opac->setStartValue(1.0);
    currentwin_opac->setEndValue(0.0);
    currentwin_opac->setEasingCurve(QEasingCurve::InOutCubic);
    currentwin_opac->setDuration(duration);

    animationGroup()->addAnimation(currentwin_pos);
    animationGroup()->addAnimation(currentwin_scale);
    animationGroup()->addAnimation(currentwin_opac);

    // reuse the default animation properties but change the values
    positionAnimation()->setDuration(duration);
    positionAnimation()->setEasingCurve(QEasingCurve::InOutCubic);

    scaleAnimation()->setStartValue(1.4);
    scaleAnimation()->setEndValue(1.0);
    scaleAnimation()->setEasingCurve(QEasingCurve::InOutCubic);
    scaleAnimation()->setDuration(duration);
    
    opacityAnimation()->setKeyValueAt(0, 0.0);
    opacityAnimation()->setKeyValueAt(0.5, 0.0);
    opacityAnimation()->setKeyValueAt(1.0, 1.0);
    opacityAnimation()->setEasingCurve(QEasingCurve::InOutCubic);
    opacityAnimation()->setDuration(duration);

    activeAnimations().append(positionAnimation());
    activeAnimations().append(scaleAnimation());
    activeAnimations().append(opacityAnimation());
    activeAnimations().append(currentwin_pos);
    activeAnimations().append(currentwin_scale);
    activeAnimations().append(currentwin_opac);
}

void MCallUiAnimation::setupCallMode(bool showWindow)
{
    bool incomingcall, ongoingcall;
    const MDeviceState &dev = static_cast<MCompositeManager*>(qApp)->deviceState();

    if (!targetWindow() || !targetWindow()->behind())
        return;
    
    if (showWindow) {
        incomingcall = dev.incomingCall();
        ongoingcall  = dev.ongoingCall();
    } else {
        incomingcall = call_mode == MCallUiAnimation::IncomingCall;
        ongoingcall  = call_mode == MCallUiAnimation::OutgoingCall;
    }

    if (!incomingcall && !ongoingcall)
        // call-ui should only be shown when there's a call beginning.
        // If we have no information assume it's an outgoing call.
        ongoingcall = true;

    MCompositeWindow* behind = targetWindow()->behind();
    if (ongoingcall) {
        call_mode = MCallUiAnimation::OutgoingCall;
        
        positionAnimation()->setTargetObject(behind);
        scaleAnimation()->setTargetObject(behind);
        opacityAnimation()->setTargetObject(behind);
        
        currentwin_pos->setTargetObject(targetWindow());
        currentwin_scale->setTargetObject(targetWindow());
        currentwin_opac->setTargetObject(targetWindow());
    } else {
        Q_ASSERT(incomingcall);
        call_mode = MCallUiAnimation::IncomingCall;
        
        positionAnimation()->setTargetObject(targetWindow());
        scaleAnimation()->setTargetObject(targetWindow());
        opacityAnimation()->setTargetObject(targetWindow());
        
        currentwin_pos->setTargetObject(behind);
        currentwin_scale->setTargetObject(behind);
        currentwin_opac->setTargetObject(behind);
    }
}

void MCallUiAnimation::windowShown()
{
    if (!targetWindow())
        return;

    setEnabled(true);
    setupBehindAnimation();
    setupCallMode();

    MStatusBarTexture::instance()->updatePixmap();
    cropper->setAppWindow(targetWindow());
    cropper->installEffect(targetWindow());
    if ((behindTarget = targetWindow()->behind()) != NULL)
        cropper->installEffect(behindTarget);
    cropper->setPortrait(targetWindow()->propertyCache()->orientationAngle() % 180);
    
    if (call_mode == MCallUiAnimation::IncomingCall)
        animationGroup()->setDirection(QAbstractAnimation::Forward);
    else if (call_mode == MCallUiAnimation::OutgoingCall)
        animationGroup()->setDirection(QAbstractAnimation::Backward);
    start();
}
 
void MCallUiAnimation::windowClosed()
{
    if (!targetWindow())
        return;
    
    setEnabled(true);
    setupBehindAnimation();
    setupCallMode(false);
    targetWindow()->setVisible(true);

    MStatusBarTexture::instance()->updatePixmap();
    cropper->setAppWindow(targetWindow());
    cropper->installEffect(targetWindow());
    if ((behindTarget = targetWindow()->behind()) != NULL)
        cropper->installEffect(behindTarget);
    cropper->setPortrait(targetWindow()->propertyCache()->orientationAngle() % 180);
    
    if (call_mode == MCallUiAnimation::IncomingCall)
        animationGroup()->setDirection(QAbstractAnimation::Backward);
    else if (call_mode == MCallUiAnimation::OutgoingCall)
        animationGroup()->setDirection(QAbstractAnimation::Forward);
    
    if (targetWindow()->behind()) {
        tempHideDesktop(targetWindow()->behind());
        targetWindow()->behind()->setVisible(true);
    }
    start();
}

void MCallUiAnimation::windowIconified()
{
    windowClosed();
}

void MCallUiAnimation::setupBehindAnimation()
{
    if (!targetWindow())
        return;
    
    MCompositeWindow* behind = targetWindow()->behind();
    if (behind) {
        tempHideDesktop(behind);
        behind->setVisible(true);
        
        // behind scale travel 
        QRect brect = behind->propertyCache()->realGeometry();
        brect.setSize(brect.size() * 0.6);
        brect.moveCenter(screen.center().toPoint());
        currentwin_pos->setStartValue(behind->propertyCache()->realGeometry().topLeft());
        currentwin_pos->setEndValue(brect.topLeft());

        // target window scale travel
        QRect trect = targetWindow()->propertyCache()->realGeometry();
        trect.setSize(trect.size() * 1.4);
        trect.moveCenter(screen.center().toPoint());
        positionAnimation()->setStartValue(trect.topLeft());
        positionAnimation()->setEndValue(targetWindow()->propertyCache()->realGeometry().topLeft());        
    }
}

// temporarily hide desktop window but make sure it is not the behind window
void MCallUiAnimation::tempHideDesktop(MCompositeWindow* behind)
{
    if (behind) {
        MCompositeWindow* d = MCompositeWindow::compositeWindow(((MCompositeManager *) qApp)->desktopWindow());
        if (d && behind != d)
            d->setVisible(false);
    }
}

void MCallUiAnimation::endAnimation()
{
    // is MCallUiAnimation disabled and this animation was triggered 
    // in a plugin?
    if (animationGroup()->animationCount() < activeAnimations().count())
        return;
    
    MCompositeManager* m = (MCompositeManager *) qApp;
    MCompositeWindow* d = MCompositeWindow::compositeWindow(m->desktopWindow());
    if (d && !d->isVisible())
        d->setVisible(true);

    if (!targetWindow())
        return;
    
    MStatusBarTexture::instance()->untrackDamages();
    cropper->removeEffect(targetWindow());    
    // reset default values
    targetWindow()->setUntransformed();
    targetWindow()->setPos(targetWindow()->propertyCache()->realGeometry().topLeft());
    MCompositeWindow* behind = behindTarget;
    behindTarget = NULL;
    if (behind) {
        cropper->removeEffect(behind);
        behind->setUntransformed();
        behind->setPos(behind->propertyCache()->realGeometry().topLeft());
    }
    // stack the call-ui when finishing the animation 
    if (targetWindow()->propertyCache()->windowState() == NormalState)
        m->positionWindow(targetWindow()->window(),
                          MCompositeManager::STACK_TOP);
    else if (targetWindow()->propertyCache()->windowState() == IconicState)
        m->positionWindow(targetWindow()->window(),
                          MCompositeManager::STACK_BOTTOM);
}

#include "mdynamicanimation.moc"
