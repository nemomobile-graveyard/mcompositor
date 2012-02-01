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

#include "mcompositewindow.h"
#include "mcompositewindowanimation.h"
#include "mcompositemanager.h"
#include "mcompositemanager_p.h"
#include "mtexturepixmapitem.h"
#include "mdecoratorframe.h"
#include "mcompositemanagerextension.h"
#include "mcompositewindowgroup.h"
#include "msplashscreen.h"
#include "mdynamicanimation.h"
#include "mdevicestate.h"

#include <QX11Info>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <X11/Xatom.h>

int MCompositeWindow::window_transitioning = 0;

MCompositeWindow::MCompositeWindow(Qt::HANDLE window, 
                                   MWindowPropertyCache *mpc, 
                                   QGraphicsItem *p)
    : QGraphicsItem(p),
      pc(mpc),
      animator(0),
      sent_ping_timestamp(0),
      received_ping_timestamp(0),
      iconified(false),
      iconify_state(NoIconifyState),
      in_destructor(false),
      window_status(Normal),
      need_decor(false),
      window_obscured(-1),
      is_transitioning(false),
      is_not_stacking(false),
      resize_expected(false),
      painted_after_mapping(false),
      allow_delete(false),
      win_id(window)
{
    const MCompositeManager *mc = static_cast<MCompositeManager*>(qApp);

    if (!mpc || (mpc && !mpc->is_valid && !mpc->isVirtual())) {
        newly_mapped = false;
        t_ping = t_reappear = damage_timer = 0;
        return;
    }
    connect(mpc, SIGNAL(iconGeometryUpdated()), SLOT(updateIconGeometry()));

    t_ping = new QTimer(this);
    t_ping->setInterval(mc->configInt("ping-interval-ms"));
    connect(t_ping, SIGNAL(timeout()), SLOT(pingTimeout()));
    t_reappear = new QTimer(this);
    t_reappear->setSingleShot(true);
    t_reappear->setInterval(mc->configInt("hung-dialog-reappear-ms"));
    connect(t_reappear, SIGNAL(timeout()), SLOT(reappearTimeout()));

    damage_timer = new QTimer(this);
    damage_timer->setSingleShot(true);
    connect(damage_timer, SIGNAL(timeout()), SLOT(damageReceived()));

    close_timer.setSingleShot(true);
    close_timer.setInterval(mc->configInt("close-timeout-ms"));
    connect(&close_timer, SIGNAL(timeout()), SLOT(closeTimeout()));

    // Newly-mapped non-decorated application windows are not initially 
    // visible to prevent flickering when animation is started.
    // We initially prevent item visibility from compositor itself
    // or it's corresponding thumbnail rendered by the switcher
    bool is_app = isAppWindow();
    newly_mapped = is_app;
    if (!pc->isInputOnly()) {
        // never paint InputOnly windows
        setVisible(!is_app); // newly_mapped used here
    }

    MCompositeWindowAnimation* a = 0;
    if (pc->windowType() == MCompAtoms::SHEET && lastVisibleParent() != None)
        a = new MSheetAnimation(this);
    else if (pc->invokedBy() != None)
        a = new MChainedAnimation(this);
    else if (pc->isCallUi())
        a = new MCallUiAnimation(this);
    else
        a = new MCompositeWindowAnimation(this);
    a->setTargetWindow(this);
    orig_animator = a;
}

MCompositeWindow::~MCompositeWindow()
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    in_destructor = true;

    endAnimation();    
    if (pc) {
        pc->damageTracking(false);
        if (p->d->prop_caches.value(win_id) == pc) {
            p->d->prop_caches.remove(win_id);
            p->d->removeWindow(win_id);
        }
        pc->deleteLater();
    }
    if (animator)
        // deleteLater() is used here as destroying the animator might trigger accessing
        // the window which is destroyed right now. When the destruction is delayed we can
        // be sure that the window is completely gone and related QPointers state this fact
        // as expected.
        animator->deleteLater();
}

/* Returns true if signal will come.
 */
bool MCompositeWindow::iconify()
{
    if (damage_timer->isActive()) {
        damage_timer->stop();
        pc->setWaitingForDamage(0);
        resize_expected = false;
    }

    if (iconify_state == ManualIconifyState) {
        setIconified(true);
        window_status = Normal;
        return false;
    }

    if (window_status != MCompositeWindow::Closing)
        window_status = MCompositeWindow::Minimizing;
    
    iconified = true;
    
    // iconify handler
    if (animator) {
        MCompositeManager *m = (MCompositeManager*)qApp;
        MCompositeWindow *d = compositeWindow(m->desktopWindow());
        if (!m->isCompositing())
            m->d->enableCompositing();
        d->updateWindowPixmap();
        animator->windowIconified();
        window_status = Normal;
        m->servergrab.commit();
        return true;
    }
    return false;
}

void MCompositeWindow::setUntransformed(bool preserve_iconified)
{
    endAnimation();
    
    newly_mapped = false;
    setVisible(true);
    setOpacity(1.0);
    setScale(1.0);
    if (!preserve_iconified)
        iconified = false;
}

void MCompositeWindow::setIconified(bool iconified)
{
    iconify_state = ManualIconifyState;
    if (iconified && !animator->pendingAnimation())
        emit itemIconified(this);
    else if (!iconified && !animator->pendingAnimation())
        iconify_state = NoIconifyState;
}

void MCompositeWindow::setWindowObscured(bool obscured, bool no_notify)
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    short new_value = obscured ? 1 : 0;
    if ((new_value == window_obscured && !newly_mapped)
        || (!obscured && p->displayOff() && !pc->lowPowerMode()))
        return;
    window_obscured = new_value;

    if (!no_notify && !pc->isVirtual()) {
        XVisibilityEvent c;
        c.type       = VisibilityNotify;
        c.send_event = True;
        c.window     = window();
        c.state      = obscured ? VisibilityFullyObscured :
                                  VisibilityUnobscured;
        XSendEvent(QX11Info::display(), window(), true,
                   VisibilityChangeMask, (XEvent *)&c);
    }
}

/*
 * We ensure that ensure there are ALWAYS updated thumbnails in the 
 * switcher by letting switcher know in advance of the presence of this window.
 * Delay the minimize animation until we receive an iconGeometry update from
 * the switcher
 */
void MCompositeWindow::startTransition()
{
    if (orig_animator == animator
        // this limitation makes sense for the default animator only
        && iconified && pc->iconGeometry().isNull())
        return;
    if (animator && animator->pendingAnimation()) {
        // don't trigger irrelevant windows
        // if (animator->targetWindow() != this)
        //     animator->setTargetWindow(this);
        MCompositeWindow::setVisible(true);
        animator->startTransition();
    }
}

void MCompositeWindow::updateIconGeometry()
{
    if (pc && pc->iconGeometry().isNull())
        return;

    // trigger transition the second time around and update animation values
    if (iconified) 
        startTransition();
}

// TODO: have an option of disabling the animation
void MCompositeWindow::restore()
{
    if (window_status == Restoring)
        return;

    setVisible(true);
    iconified = false;
     // Restore handler
    MCompositeManager *mc = static_cast<MCompositeManager *>(qApp);
    if (animator && !mc->splashed(this)) {
        updateWindowPixmap();
        window_status = Restoring;
        animator->windowRestored();
        mc->servergrab.commit();
    }
}

void MCompositeWindow::waitForPainting()
{
    const MCompositeManager *mc = static_cast<MCompositeManager*>(qApp);
    setWindowObscured(false);
    // waiting for two damage events seems to work for Meegotouch apps
    // at least, for the rest, there is a timeout
    pc->setWaitingForDamage(mc->configInt("damages-for-starting-anim"));
    resize_expected = false;
    painted_after_mapping = false;
    damage_timer->setInterval(mc->configInt("damage-timeout-ms"));
    damage_timer->start();
}

bool MCompositeWindow::showWindow()
{
    if (type() == MSplashScreen::Type) {
        beginAnimation();
        MCompositeManager *mc = static_cast<MCompositeManager*>(qApp);
        mc->servergrab.grabLater(false);
        q_fadeIn();
        return true;
    }

    if (!pc 
        || !pc->is_valid
        || (!isAppWindow() && pc->invokedBy() == None)
        // isAppWindow() returns true for system dialogs
        || pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DIALOG)) {
        painted_after_mapping = true;
        return false;
    }

    beginAnimation();
    if (!painted_after_mapping && (!animator || !animator->isActive())) {
        waitForPainting();
        setWindowObscured(false);
    } else {
        painted_after_mapping = true;
        q_fadeIn();
    }
    return true;
}

void MCompositeWindow::expectResize()
{
    const MCompositeManager *mc = static_cast<MCompositeManager*>(qApp);

    // In addition to @waiting_for_damages, also wait for a resize().
    // Be nice and wait some more because mdecorator has a huge latency.
    if (!damage_timer->isActive())
        return;
    resize_expected = true;
    damage_timer->setInterval(mc->configInt("expect-resize-timeout-ms"));
}

void MCompositeWindow::damageReceived()
{
    emit damageReceived(this);
    if (!pc->waitingForDamage() && !resize_expected) {
        // We aren't planning to show the window.
        Q_ASSERT(!damage_timer->isActive());
        // triggers switch from XDamageReportRawRectangles to XDamageReportNonEmpty
        // if needed
        pc->setWaitingForDamage(0);
        return;
    } else if (damage_timer->isActive()) {
        // We're within timeout and just got a damage.
        Q_ASSERT(pc->waitingForDamage() > 0);
        int waiting_for_damage = pc->waitingForDamage();
        pc->setWaitingForDamage(--waiting_for_damage);
        if (waiting_for_damage || resize_expected)
            // Conditions haven't been met yet.
            return;
    }
    painted_after_mapping = true;

    // Either timeout or the conditions have been met.
    Q_ASSERT(!damage_timer->isActive() ||
             (!pc->waitingForDamage() && !resize_expected));
    damage_timer->stop();
    pc->setWaitingForDamage(0);

    resize_expected = false;

    MCompositeManager *m = static_cast<MCompositeManager*>(qApp);
    if (pc->isLockScreen()) {
        newly_mapped = false;
        setVisible(true);
        endAnimation();
        m->lockScreenPainted();
        m->possiblyUnredirectTopmostWindow();
        return;
    }

    // We're ready to take over the splash screen.
    MCompositeWindow *splash;
    splash = m->splashed(this);
    if (splash && !m->d->dismissedSplashScreens.contains(pc->pid())) {
        setVisible(true);
        splash->windowAnimator()->crossFadeTo(this);
        newly_mapped = false;

        // setting up the cross fade animation could have finished
        // minimizing the splash screen - recheck if it is now dismissed
        if (m->d->dismissedSplashScreens.contains(pc->pid())) {
            m->d->dismissedSplashScreens[pc->pid()].blockTimer.start();
            endAnimation();
            m->possiblyUnredirectTopmostWindow();
            m->setWindowState(pc->winId(), IconicState);
            m->positionWindow(pc->winId(), MCompositeManager::STACK_BOTTOM);
        }
    } else if (isInanimate(false)) {
        newly_mapped = false;
        setVisible(true);
        endAnimation();
        m->recheckVisibility();
        m->possiblyUnredirectTopmostWindow();
    } else
        q_fadeIn();
}

void MCompositeWindow::resize(int, int)
{
    if (!resize_expected)
        return;
    if (!pc->waitingForDamage()) {
        // We got the expected resize and the damages have arrived too.
        // Simulate a timeout to kick the animation.
        damage_timer->stop();
        damageReceived();
    } else
        resize_expected = false;
}

void MCompositeWindow::q_fadeIn()
{
    newly_mapped = false;
    setVisible(true);
    updateWindowPixmap();
    newly_mapped = true;
    
    iconified = false;

    // fade-in handler
    MCompositeManager *mc = static_cast<MCompositeManager*>(qApp);
    if (animator && isMapped()) {
        if (!static_cast<MCompositeManager*>(qApp)->isCompositing())
            static_cast<MCompositeManager*>(qApp)->d->enableCompositing();
        // always ensure the animation is visible. zvalues get corrected later 
        // at checkStacking 
        setZValue(mc->d->stacking_list.size()+1);
        animator->windowShown();
        mc->servergrab.commit();
    } else {
        endAnimation();
        mc->possiblyUnredirectTopmostWindow();
    }
}

bool MCompositeWindow::isInanimate(bool check_pixmap)
{
    if (static_cast<MCompositeManager*>(qApp)->displayOff())
        return true;
    if (pc->isInputOnly() || pc->isOverrideRedirect() || pc->noAnimations() ||
        pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_MENU) ||
        pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DIALOG))
        return true;
    if (pc->windowType() == MCompAtoms::SHEET)
        return false;
    if (pc->isCallUi())
        return false;
    if (pc->invokedBy() != None)
        return false;
    if (!pc->is_valid)
        return true;
    if (window_status == MCompositeWindow::Hung
          || pc->windowState() == IconicState)
        return true;
    if (check_pixmap && !windowPixmap())
        return true;
    // isAppWindow() returns true for system dialogs
    if (!isAppWindow() && !pc->invokedBy())
        return true;
    return false;
}

void MCompositeWindow::closeWindowRequest()
{
    if (!pc || !pc->is_valid || (!isMapped() && !pc->beingMapped()))
        return;
    if (!isInanimate(false)) {
        // get a Pixmap for the possible unmap animation
        MCompositeManager *p = (MCompositeManager *) qApp;
        if (!p->isCompositing())
            p->d->enableCompositing();
        updateWindowPixmap();
    }
    emit closeWindowRequest(this);
}

void MCompositeWindow::closeWindowAnimation()
{
    if (window_status == Closing || window_obscured || isInanimate()
            // We do not want to influence currently ongoing animations.
            // After they are finished this window will be automatically
            // either invisible or instantly disappeares without animation.
            || is_transitioning)
        return;
    window_status = Closing; // animating, do not disturb

    MCompositeManager *p = (MCompositeManager *) qApp;
    setVisible(true);
    
    // fade-out handler
    MCompositeWindow *d = compositeWindow(p->desktopWindow());
    if (animator && d) {
        if (!p->isCompositing()) {
            p->d->enableCompositing();
            updateWindowPixmap();
        }
        d->updateWindowPixmap();
        animator->windowClosed();
        window_status = Normal;
        p->servergrab.grabLater(animator->grabAllowed());
        p->servergrab.commit();
    }
}

bool MCompositeWindow::event(QEvent *e)
{
    if (e->type() == QEvent::DeferredDelete && is_transitioning
        && !allow_delete) {
        // Can't delete the object yet, try again in the next iteration.
        deleteLater();
        return true;
    } else
        return QObject::event(e);
}

void MCompositeWindow::finalizeState()
{
    if (in_destructor)
        return;
    // as far as this window is concerned, it's OK to direct render
    window_status = Normal;

    // iconification status
    if (iconified) {
        iconified = false;
        hide();
        iconify_state = TransitionIconifyState;
        emit itemIconified(this);
    } else {
        iconify_state = NoIconifyState;
        show();
        // no delay: window does not need to be repainted when restoring
        // from the switcher (even then the animation should take long enough
        // to allow it)
        q_itemRestored();
    }
}

void MCompositeWindow::q_itemRestored()
{
    emit itemRestored(this);
}

void MCompositeWindow::requestZValue(int zvalue)
{
    // when animating, Z-value is set again after finishing the animation
    // (setting it later in finalizeState() caused flickering)
    if (animator && !animator->isActive() && !animator->pendingAnimation())
        setZValue(zvalue);
}

void MCompositeWindow::setVisible(bool visible)
{
    if ((pc && pc->isInputOnly())
        || (visible && newly_mapped && isAppWindow()) 
        || (!visible && is_transitioning)) 
        return;

    bool old_value = isVisible();
    QGraphicsItem::setVisible(visible);
    MCompositeManager *p = (MCompositeManager *) qApp;
    p->d->setWindowDebugProperties(window());

    QGraphicsScene* sc = scene();    
    if (sc && !visible && sc->items().count() == 1)
        clearTexture();
    else if (visible && old_value != visible)
        // handle old damage that possibly came while we were invisible
        updateWindowPixmap();
}

void MCompositeWindow::startPing(bool restart)
{
    if (restart)
        stopPing();
    else if (t_ping->isActive())
        // this function can be called repeatedly without extending the timeout
        return;
    // startup: send ping now, otherwise it is sent after timeout
    pingWindow(restart);
    t_ping->start();
}

void MCompositeWindow::stopPing()
{
    if (t_ping) t_ping->stop();
    if (t_reappear) t_reappear->stop();
}

void MCompositeWindow::startDialogReappearTimer()
{
    if (window_status != Hung)
        return;
    t_reappear->start();
}

void MCompositeWindow::reappearTimeout()
{
    if (window_status == Hung)
        // show "application not responding" UI again
        emit windowHung(this, true);
}

void MCompositeWindow::closeTimeout()
{
    MCompositeManager *p = (MCompositeManager*)qApp;
    window_status = Hung;
    p->d->closeHandler(this); // kill
}

void MCompositeWindow::startCloseTimer()
{
    close_timer.start();
}

void MCompositeWindow::stopCloseTimer()
{
    close_timer.stop();
}

void MCompositeWindow::receivedPing(ulong serverTimeStamp)
{
    received_ping_timestamp = serverTimeStamp;
    
    if (window_status == Hung) {
        window_status = Normal;
        emit windowHung(this, false);
    }
    t_reappear->stop();
}

void MCompositeWindow::pingTimeout()
{
    if (received_ping_timestamp < sent_ping_timestamp
        && pc && pc->isMapped() && window_status != Hung
        && window_status != Minimizing && window_status != Closing) {
        window_status = Hung;
        emit windowHung(this, true);
    }
    if (t_ping->isActive())
        // interval timer is still active
        pingWindow();
}

void MCompositeWindow::pingWindow(bool restart)
{
    if (window_status == Hung && !restart)
        // don't send a new ping before the window responds, otherwise we may
        // queue up too many of them
        return;
    // It takes 5*4294967295s or 248551.35 days before it overflows.
    // Don't worry, you're not even remotely in danger on this platform.
    ulong t = ++sent_ping_timestamp;
    Window w = window();

    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = ATOM(WM_PROTOCOLS);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = ATOM(_NET_WM_PING);
    ev.xclient.data.l[1] = t;
    ev.xclient.data.l[2] = w;

    XSendEvent(QX11Info::display(), w, False, NoEventMask, &ev);
}

MCompositeWindow::WindowStatus MCompositeWindow::status() const
{
    return window_status;
}

bool MCompositeWindow::needDecoration() const
{
    return need_decor;
}

bool MCompositeWindow::needsCompositing() const
{
    if (!pc || (!pc->is_valid && !pc->isVirtual()))
        return false;
    if (need_decor || (pc->isDecorator() && !pc->opaqueWindow()))
        return true;
    if (pc->hasAlphaAndIsNotOpaque() && !pc->lowPowerMode())
        return true;
    return false;
}

void MCompositeWindow::setDecorated(bool decorated)
{
    need_decor = decorated;
}

MCompositeWindow *MCompositeWindow::compositeWindow(Qt::HANDLE window)
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    return p->d->windows.value(window, 0);
}

void MCompositeWindow::beginAnimation()
{
    if (!in_destructor && type() == MSplashScreen::Type) {
        ((MSplashScreen*)this)->beginAnimation();
    }
    if (!isMapped() && window_status != Closing)
        return;

    if (!is_transitioning) {
        if (!window_transitioning)
            emit firstAnimationStarted();
        ++window_transitioning;
        is_transitioning = true;
    }
}

void MCompositeWindow::endAnimation()
{    
    if (!in_destructor && type() == MSplashScreen::Type) {
        ((MSplashScreen*)this)->endAnimation();
    }
    if (is_transitioning) {
        is_transitioning = false;
        --window_transitioning;
        if (!window_transitioning)
            emit lastAnimationFinished(this);
    }
}

bool MCompositeWindow::hasTransitioningWindow()
{
    return window_transitioning > 0;
}

QVariant MCompositeWindow::itemChange(GraphicsItemChange change, const QVariant &value)
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    if (change == ItemZValueHasChanged) {
        p->d->setWindowDebugProperties(window());
    }

    if (change == ItemVisibleHasChanged) {
        // Be careful not to update if this item whose visibility is about
        // to change is behind a visible item, to not reopen NB#189519.
        // Update is needed if visibility changes for a visible item
        // (other visible items get redrawn also).  Case requiring this:
        // status menu closed on top of an mdecorated window.
        bool ok_to_update = true;
        if (scene()) {
            QList<QGraphicsItem*> l = scene()->items();
            for (QList<QGraphicsItem*>::const_iterator i = l.begin();
                 i != l.end(); ++i)
                if ((*i)->isVisible()) {
                    ok_to_update = zValue() >= (*i)->zValue();
                    break;
                }
        }

        if (ok_to_update)
            // Nothing is visible or the topmost visible item is lower than us.
            p->d->glwidget->update();
    }

    return QGraphicsItem::itemChange(change, value);
}

void MCompositeWindow::update()
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    p->d->glwidget->update();
}

bool MCompositeWindow::isAppWindow(bool include_transients)
{
    if (pc && (pc->is_valid || pc->isVirtual()))
        return pc->isAppWindow(include_transients);
    else
        return false;
}

QPainterPath MCompositeWindow::shape() const
{    
    QPainterPath path;
    const QRegion &shape = propertyCache()->shapeRegion();
    if (QRegion(boundingRect().toRect()).subtracted(shape).isEmpty())
        path.addRect(boundingRect());
    else
        path.addRegion(shape);
    return path;
}

Window MCompositeWindow::lastVisibleParent() const
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    if (pc && pc->is_valid)
        return p->d->getLastVisibleParent(pc);
    else
        return None;
}

int MCompositeWindow::indexInStack() const
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    return p->d->stacking_list.indexOf(window());
}

MCompositeWindow * MCompositeWindow::behind() const
{
    MCompositeWindow *behindWindow = 0;
    MCompositeManager *p = (MCompositeManager *) qApp;
    for (int behind_i = indexInStack() - 1; behind_i >= 0; --behind_i) {
        Window behind_w = p->d->stacking_list.at(behind_i);
        MCompositeWindow* w = MCompositeWindow::compositeWindow(behind_w);
        if (!w) continue;
        if (w->propertyCache()->windowState() == NormalState
            && w->propertyCache()->isMapped()
            && !w->propertyCache()->isDecorator()) {
            behindWindow = w;
            break;
        }
        else if (w->propertyCache()->isDecorator() &&
                   MDecoratorFrame::instance()->managedClient()) {
            behindWindow = MDecoratorFrame::instance()->managedClient();
            break;
        }
        else
            continue;
    }

    return behindWindow;
}

void MCompositeWindow::setIsMapped(bool mapped) 
{ 
    if (mapped)
        window_status = Normal; // make sure Closing -> Normal when remapped
    if (pc) pc->setIsMapped(mapped); 
}

bool MCompositeWindow::isMapped() const 
{
    return pc ? pc->isMapped() : false;
}

MCompositeWindowGroup* MCompositeWindow::group() const
{
#ifdef GLES2_VERSION
    return renderer()->current_window_group;
#else
    return 0;
#endif
}

MCompositeWindowAnimation* MCompositeWindow::windowAnimator() const
{
    return animator;
}
