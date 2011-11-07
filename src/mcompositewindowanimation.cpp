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
/*!
  \class MCompositeWindowAnimation
  \brief MCompositeWindowAnimation class which provides full control 
  of position transformation and opacity animations of composite window objects.

  To create more complex animations, re-implement the virtual functions
  windowShown(), windowClosed(), widowIconified() and windowRestored(); 
  A QParallelAnimationGroup object is provided which can be a container
  for more complex animations for more flexibility.
*/
// TODO: import icongeometry. export signals when animation done to hook
// to composting on off

#include <QMetaMethod>
#include <QRectF>
#include <QDesktopWidget>
#include <QPropertyAnimation>
#include <mcompositewindow.h>
#include <mcompositewindowanimation.h>
#include <QApplication>
#include <QParallelAnimationGroup>
#include <mcompositemanager.h>
#include <mcompositemanager_p.h>
#include <QVector>

static QAtomicInt animrefcount;

class McParallelAnimation: public QParallelAnimationGroup
{
public:
    McParallelAnimation(MCompositeWindowAnimation* p)
        :QParallelAnimationGroup(p),
         parent(p)
    {}

    ~McParallelAnimation()
    {
        if (crossfadeTarget)
            crossfadeTarget->setOpacity(1);
        if (state() != QAbstractAnimation::Stopped)
            animrefcount.deref();
    }
    void setCrossfadeTarget(MCompositeWindow *cw)
    {
        crossfadeTarget = cw;
    }
        
protected:
    void updateCurrentTime(int currentTime)
    {        
        MCompositeWindow::update();
        QParallelAnimationGroup::updateCurrentTime(currentTime);
    }

    void updateState(QAbstractAnimation::State newState, 
                     QAbstractAnimation::State oldState)
    {   
        if (newState == QAbstractAnimation::Running && 
            oldState == QAbstractAnimation::Stopped) {
            parent->ensureAnimationVisible();
            if (parent->targetWindow()) {
                animrefcount.ref();
                parent->setManuallyUpdated(false);
                parent->targetWindow()->beginAnimation();
                parent->requestStackTop();
            }
            if (parent->targetWindow2())
                parent->targetWindow2()->beginAnimation();
            emit parent->animationStarted(parent);
        } else if (newState == QAbstractAnimation::Paused &&
                   oldState == QAbstractAnimation::Running) {
            parent->setManuallyUpdated(true);
        } else if (newState == QAbstractAnimation::Stopped) {
            animrefcount.deref();
            if (parent->targetWindow()) {
                parent->setManuallyUpdated(false);
                parent->targetWindow()->endAnimation();
            }
            if (parent->targetWindow2())
                parent->targetWindow2()->endAnimation();
            // make avoiding NB#275420 possible
            emit parent->animationStopped(parent);
        } 
        QParallelAnimationGroup::updateState(newState, oldState);
    }
private:
    MCompositeWindowAnimation* parent;
    QPointer<MCompositeWindow> crossfadeTarget;
};

class MCompositeWindowAnimationPrivate: public QObject
{
public:    
    MCompositeWindowAnimationPrivate(MCompositeWindowAnimation* animation)
        : QObject(animation),
          crossfade(0),
          pending_animation(MCompositeWindowAnimation::NoAnimation),
          is_replaceable(true),
          manually_updated(false),
          animhandler(MCompositeWindowAnimation::AnimationTotal, 0)
    {
        const MCompositeManager *mc = static_cast<MCompositeManager*>(qApp);
        // default duration of 1 ms to keep "NOP animation" short 
        int duration = 1;
        if (!mc->hasPlugins())
            // dont bork the animation if we dont have a plugin
            duration = mc->configInt("startup-anim-duration");

        scale = new QPropertyAnimation(animation);
        scale->setPropertyName("scale");
        scale->setDuration(duration);
        scale->setStartValue(1);
        scale->setEndValue(1);

        position = new QPropertyAnimation(animation);
        position->setPropertyName("pos");
        position->setDuration(duration);
        position->setStartValue(QPoint(0, 0));
        position->setEndValue(QPoint(0, 0));

        opacity = new QPropertyAnimation(animation);
        opacity->setPropertyName("opacity");
        opacity->setDuration(duration);
        opacity->setStartValue(1);
        opacity->setEndValue(1);

        scalepos = new McParallelAnimation(animation);
        scalepos->addAnimation(scale);
        scalepos->addAnimation(position);
        scalepos->addAnimation(opacity);

        QObject::connect(scalepos, SIGNAL(finished()), animation,
                         SLOT(finalizeState()));
    }

    void setTargetWindow(MCompositeWindow* window)
    {
        if (scale && position && opacity) {
            scale->setTargetObject(window);
            position->setTargetObject(window);
            opacity->setTargetObject(window);
        }
    }

    bool handledByInvoker(MCompositeWindowAnimation::AnimationType type)
    {
        MAbstractAnimationHandler* handler = animhandler[type];
        if (handler) {
            handler->target_window = target_window;
            switch (type) {
            case MCompositeWindowAnimation::Showing:
                handler->windowShown();
                return true;
            case MCompositeWindowAnimation::Closing:
                handler->windowClosed();
                return true;
            case MCompositeWindowAnimation::Iconify:
                handler->windowIconified();
                return true;
            case MCompositeWindowAnimation::Restore:
                handler->windowRestored();
                return true;
            default:
                break;
            }
        }
        
        return false;
    }
   
public:
    
    QPointer<MCompositeWindow> target_window, target_window2;
    QPointer<QPropertyAnimation> scale;
    QPointer<QPropertyAnimation> position;
    QPointer<QPropertyAnimation> opacity;
    McParallelAnimation* scalepos, *crossfade;
    MCompositeWindowAnimation::AnimationType pending_animation;
    bool is_replaceable;
    bool manually_updated;
    QVector< MAbstractAnimationHandler* > animhandler;
};

MCompositeWindowAnimation::MCompositeWindowAnimation(QObject* parent)
    :QObject(parent),
     d_ptr(new MCompositeWindowAnimationPrivate(this))
{
}

MCompositeWindowAnimation::~MCompositeWindowAnimation()
{
}

bool MCompositeWindowAnimation::hasActiveAnimation()
{
    return animrefcount;
}

void MCompositeWindowAnimation::setTargetWindow(MCompositeWindow* window)
{
    Q_D(MCompositeWindowAnimation);

    // never override a non-replaceable animation
    if (window->animator && !window->animator->isReplaceable()) {
        deleteLater();
        return;
    }
    
    // replace the old animator if there is one
    if (window->animator && (window->animator != this))
        delete window->animator;

    d->target_window = window;
    d->target_window->animator = this;
    disconnect(SIGNAL(q_finalizeState()));
    connect(this, SIGNAL(q_finalizeState()), window, SLOT(finalizeState()));
    
    d->setTargetWindow(window);
}

void MCompositeWindowAnimation::finalizeState()
{
    emit q_finalizeState();
}

// returns a group animation for this animator 
QParallelAnimationGroup* MCompositeWindowAnimation::animationGroup() const
{
    Q_D(const MCompositeWindowAnimation);
    return d->scalepos;
}

MCompositeWindow* MCompositeWindowAnimation::targetWindow() const
{
    Q_D(const MCompositeWindowAnimation);
    return d->target_window;
}

MCompositeWindow* MCompositeWindowAnimation::targetWindow2() const
{
    Q_D(const MCompositeWindowAnimation);
    return d->target_window2;
}

// Exposed animation properties
QPropertyAnimation* MCompositeWindowAnimation::scaleAnimation() const
{
    Q_D(const MCompositeWindowAnimation);
    return d->scale;
}

QPropertyAnimation* MCompositeWindowAnimation::positionAnimation() const
{
    Q_D(const MCompositeWindowAnimation);
    return d->position;
}

QPropertyAnimation* MCompositeWindowAnimation::opacityAnimation() const
{
    Q_D(const MCompositeWindowAnimation);
    return d->opacity;
}

// Default effect
void MCompositeWindowAnimation::windowShown()
{
#define OPAQUE 1.0
#define DIMMED 0.1
    Q_D(MCompositeWindowAnimation);
    if (!d->target_window)
        return;

    if (d->handledByInvoker(Showing))
        return;
    
    const qreal scaleStart = 0.2;
    const QRectF &iconGeometry = d->target_window->propertyCache()->iconGeometry();
    QPointF topLeft = iconGeometry.topLeft();

    if (iconGeometry.isEmpty()) {
        const QRectF d = QApplication::desktop()->availableGeometry();
        topLeft.setX(d.width()/2.0f * (1.0f-scaleStart));
        topLeft.setY(d.height()/2.0f * (1.0f-scaleStart));
    }

    d->target_window->setPos(topLeft);
    positionAnimation()->setEasingCurve(QEasingCurve::OutQuad);
    positionAnimation()->setStartValue(topLeft);
    positionAnimation()->setEndValue(
        QPointF(d->target_window->propertyCache()->realGeometry().x(),
                d->target_window->propertyCache()->realGeometry().y()));
    scaleAnimation()->setEasingCurve(QEasingCurve::OutQuad);
    
    // TODO: use icon geometry signal
    scaleAnimation()->setStartValue(scaleStart);
    scaleAnimation()->setEndValue(1.0);
    opacityAnimation()->setEasingCurve(QEasingCurve::OutQuad);
    opacityAnimation()->setStartValue(DIMMED);
    opacityAnimation()->setEndValue(OPAQUE);

    animationGroup()->setDirection(QAbstractAnimation::Forward);
    animationGroup()->start();
}

void MCompositeWindowAnimation::windowClosed()
{
    Q_D(MCompositeWindowAnimation);
    if (d->handledByInvoker(Closing))
        return;

    positionAnimation()->setEasingCurve(QEasingCurve::InQuad);
    scaleAnimation()->setEasingCurve(QEasingCurve::InQuad);
    opacityAnimation()->setEasingCurve(QEasingCurve::InQuad);
    animationGroup()->setDirection(QAbstractAnimation::Backward);
    animationGroup()->start();
}

void MCompositeWindowAnimation::windowIconified()
{
    Q_D(MCompositeWindowAnimation);

    if (d->handledByInvoker(Iconify))
        return;
    positionAnimation()->setEasingCurve(QEasingCurve::InQuad);
    scaleAnimation()->setEasingCurve(QEasingCurve::InQuad);
    opacityAnimation()->setEasingCurve(QEasingCurve::InQuad);
    animationGroup()->setDirection(QAbstractAnimation::Backward);
    animationGroup()->start();
}

void MCompositeWindowAnimation::windowRestored()
{
    Q_D(MCompositeWindowAnimation);

    if (d->handledByInvoker(Restore))
        return;
    positionAnimation()->setEasingCurve(QEasingCurve::OutQuad);
    scaleAnimation()->setEasingCurve(QEasingCurve::OutQuad);
    opacityAnimation()->setEasingCurve(QEasingCurve::OutQuad);
    animationGroup()->setDirection(QAbstractAnimation::Forward);
    animationGroup()->start();
}

void MCompositeWindowAnimation::crossFadeTo(MCompositeWindow *cw)
{
    Q_D(MCompositeWindowAnimation);
    const MCompositeManager *mc = static_cast<MCompositeManager*>(qApp);

    if (animationGroup()->state() == QAbstractAnimation::Running) {
        animationGroup()->setCurrentTime(animationGroup()->duration());
        animationGroup()->stop();
    }

    if (d->crossfade)
        delete d->crossfade;
    d->crossfade = new McParallelAnimation(this);

    QPropertyAnimation *op = new QPropertyAnimation(this);
    op->setTargetObject(cw);
    op->setEasingCurve(QEasingCurve::Linear);
    op->setPropertyName("opacity");
    op->setDuration(mc->configInt("crossfade-duration"));
    op->setStartValue(0);
    op->setEndValue(1);
    d->crossfade->addAnimation(op);
    d->crossfade->setCrossfadeTarget(cw);

    d->target_window2 = cw;
    d->target_window2->show();

    d->crossfade->setDirection(QAbstractAnimation::Forward);

    d->pending_animation = CrossFade;
    d->crossfade->start();
    d->pending_animation = NoAnimation;
}

void MCompositeWindowAnimation::requestStackTop()
{
    if (targetWindow() && 
        targetWindow()->status() == MCompositeWindow::Restoring) {
        MCompositeManager *m = (MCompositeManager*) qApp;
        m->positionWindow(targetWindow()->window(), MCompositeManager::STACK_TOP);
    }
}

void MCompositeWindowAnimation::startTransition()
{        
    Q_D(MCompositeWindowAnimation);
    
    switch (d->pending_animation) {
    case Showing:
        windowShown();
        d->pending_animation = NoAnimation;
        break;
    case Closing:
        windowClosed();
        d->pending_animation = NoAnimation;
        break;
    case Iconify:
        windowIconified();
        d->pending_animation = NoAnimation;
        break;
    case Restore:
        windowRestored();
        d->pending_animation = NoAnimation;
        break;
    case CrossFade:
        if (targetWindow2())
            targetWindow2()->show();
        d->crossfade->start();
        d->pending_animation = NoAnimation;
        break;
    default:  break;
    }
}

void MCompositeWindowAnimation::ensureAnimationVisible()
{
    // Always ensure the animation is REALLY visible. Z-values get corrected 
    // later at checkStacking if needed
    MCompositeWindow *t = targetWindow();
    if (t && (t->status() == MCompositeWindow::Restoring ||
              t->propertyCache()->windowState() != IconicState))
        targetWindow()->setZValue(
              ((MCompositeManager*)qApp)->d->stacking_list.size() + 1);
    if (targetWindow2())
        targetWindow2()->setZValue(
              ((MCompositeManager*)qApp)->d->stacking_list.size() + 2);
}

// plays the animation group;
void MCompositeWindowAnimation::start()
{
    animationGroup()->start();
}

// pauses the animation group;
void MCompositeWindowAnimation::pause()
{
    animationGroup()->pause();
}

void MCompositeWindowAnimation::finish()
{
  QParallelAnimationGroup *ag = animationGroup();

  // Let the timeline's finish() run.
  ag->setDirection(QAbstractAnimation::Backward);
  ag->start();
  ag->setCurrentTime(0);
}

bool MCompositeWindowAnimation::isActive()
{
    return (animationGroup()->state() != QAbstractAnimation::Stopped);
}

MCompositeWindowAnimation::AnimationType MCompositeWindowAnimation::pendingAnimation() const 
{ 
    Q_D(const MCompositeWindowAnimation);
    
    return d->pending_animation; 
}

/*!
  Enabled or disables this animation. Re-implement this function to customize
  how a custom animation can be disabled or disabled
 */
void MCompositeWindowAnimation::setEnabled(bool enabled)
{
    if (enabled) {
        if (animationGroup()->indexOfAnimation(positionAnimation()) == -1)
            animationGroup()->addAnimation(positionAnimation());
        if (animationGroup()->indexOfAnimation(scaleAnimation()) == -1)
            animationGroup()->addAnimation(scaleAnimation());
        if (animationGroup()->indexOfAnimation(opacityAnimation()) == -1)
            animationGroup()->addAnimation(opacityAnimation());
    } else if (!enabled) {
        animationGroup()->stop();
        animationGroup()->removeAnimation(positionAnimation());
        animationGroup()->removeAnimation(scaleAnimation());
        animationGroup()->removeAnimation(opacityAnimation());
    }
}

/*!
  \return Whether this animation can be replaced or not. By default, window
  animations can be replaced with a custom animation 
 */
bool MCompositeWindowAnimation::isReplaceable() const
{
    Q_D(const MCompositeWindowAnimation);
    
    return d->is_replaceable;
}

/*!
   If \a replaceable is true, this animation can be replaced with another custom
   animation object. If \a replaceable is false, the animation may not be 
   replaced with another animation and will be set for the lifetime of the 
   window that is initially associated with it.
 */
void MCompositeWindowAnimation::setReplaceable(bool replaceable)
{
    Q_D(MCompositeWindowAnimation);

    d->is_replaceable = replaceable;
}

/*!
   Sets a custom external animation handler for animation \a type. If an 
   external handler is set it will use the virtual functions of that animation
   \a handler instead of this object's handlers. 
   To create an external handler, inherit from MAbstractAnimationHandler class
   and reimplement the required animation functions as needed.
 */
void MCompositeWindowAnimation::setAnimationHandler(AnimationType type, 
                                                    MAbstractAnimationHandler* handler)
{
    Q_D(MCompositeWindowAnimation);

    handler->main_animator = this;
    d->animhandler[type] = handler;
}

void MCompositeWindowAnimation::disconnectHandler(MAbstractAnimationHandler* handler)
{
    Q_D(MCompositeWindowAnimation);
    int type = d->animhandler.indexOf(handler);
    if (type > -1)
        d->animhandler[type] = 0;
}

bool MCompositeWindowAnimation::isManuallyUpdated() const
{
    Q_D(const MCompositeWindowAnimation);
    return d->manually_updated;
}

void MCompositeWindowAnimation::setManuallyUpdated(bool updatemode)
{
    Q_D(MCompositeWindowAnimation);
    d->manually_updated = updatemode;
}

void MAbstractAnimationHandler::windowShown()
{// NOOP
}

void MAbstractAnimationHandler::windowClosed()
{// NOOP
}

void MAbstractAnimationHandler::windowIconified() 
{// NOOP
}

void MAbstractAnimationHandler::windowRestored()
{// NOOP
}

MCompositeWindow* MAbstractAnimationHandler::targetWindow() const
{
    return target_window;
}

MAbstractAnimationHandler::~MAbstractAnimationHandler()
{
    if (main_animator)
        main_animator->disconnectHandler(this);
}

