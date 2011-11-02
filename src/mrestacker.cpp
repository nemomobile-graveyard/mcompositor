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

#include "mcompositemanager.h"
#include <QSet>
#include "mrestacker.h"
#include <X11/Xproto.h> // X_ConfigureWindow

// Enable or disable debugging messages.
#define RESTACKER_LOG(txt, args... )                        /* NOP */
#define RESTACKER_LOG_IF(cond, txt, args... )               /* NOP */

// Import MRestacker's typedefs for the static functions.
typedef MRestacker::WindowStack WindowStack;
typedef MRestacker::WindowOrder WindowOrder;
typedef MRestacker::OrderedWindowStack OrderedWindowStack;
typedef MRestacker::StackOps StackOps;

// Used to assert the equivalence of %OrderedWindowStack:s.
static __attribute__((unused))
bool operator==(const WindowOrder &lhs, const WindowOrder &rhs)
{
    return !memcmp(&lhs, &rhs, sizeof(lhs));
}

// Tell %MRestacker the X connection to use, and the parent of all
// windows in the @state.  In absence of @dpy we're in mock mode,
// which makes restack() always successful without touching X.
void MRestacker::init(Display *dpy, Window root)
{
    this->dpy  = dpy;
    this->root = dpy && !root ? DefaultRootWindow(dpy) : root;
    this->state.clear();
    this->sentinel.above = this->sentinel.below = None;
    this->dirtyState = false;
    this->skipToEvent = 0;
    resetStats();
}

void MRestacker::resetStats()
{
    memset(&conStats, 0, sizeof(conStats));
    memset(&altStats, 0, sizeof(altStats));
}

QString MRestacker::PlannerStatistics::toString() const
{
    return QString().sprintf("nplans: %4u, nstackops: %5u, avgsaves: %2d%%",
                   nplans, nstackops, int(100.0 - 100.0*duties/nplans));
}

// Tell %MRestacker the current stacking.  If a @subset is specified,
// only those @wins that are in it too will be entered into the @state.
// If @bottomFirst then @wins[0] is taken as the bottom of the stack.
void MRestacker::setState(const Window *wins, unsigned nwins,
                          bool bottomFirst, const OrderedWindowStack *subset)
{
    Q_ASSERT(wins || !nwins);

    // Generate ordered window stack with fast access.
    state.clear();
    WindowOrder order = { None, None };
    sentinel.above = sentinel.below = None;
    for (unsigned o = 0; o < nwins; o++) {
        unsigned i = bottomFirst ? o : nwins-1-o;
        Window between = wins[i];

        if (subset && !subset->contains(between))
            continue;
        Q_ASSERT(between != None);
        Q_ASSERT(!state.contains(between));

        if (!sentinel.below)
            sentinel.below = between;
        sentinel.above = between;

        state[between] = order;
        if (order.below != None)
            state[order.below].above = between;
        order.below = between;
    }

    dirtyState = false;
}

// Copy the state of the @other %MRestacker.
void MRestacker::setState(const MRestacker &other)
{
    state = other.state;
    sentinel = other.sentinel;
    dirtyState = false;
}

// Synchronize @state from X.  If @allChildren, all children of @root
// will be entered into the @state; otherwise, only the order of the
// windows already in @state is updated, and non-existing windows are
// removed.
bool MRestacker::syncState(bool allChildren)
{
    Window foo;
    Window *wins;
    unsigned nwins;

    // Try the get the current stacking order of windows.
    Q_ASSERT(dpy != NULL);
    unsigned long req = NextRequest(dpy);
    if (!XQueryTree(dpy, root, &foo, &foo, &wins, &nwins)) {
        qCritical("MRestacker: XQueryTree() failed");
        return false;
    }

    OrderedWindowStack *subset = allChildren
        ? NULL : new OrderedWindowStack(state);
    setState(wins, nwins, subset);
    delete subset;
    XFree(wins);

    // Ignore all XEvent:s generated up to this point,
    // since our knowledge is ahead of them.
    skipToEvent = req;
    return true;
}

// Get the @state in bottom-first order.
WindowStack MRestacker::getState() const
{
    WindowStack stack;
    for (Window win = sentinel.below; win != None; win = state[win].above)
        stack << win;
    return stack;
}

// Verifies that @state conforms to @wins: that it contains exactly
// those windows and exactly in that order.
bool MRestacker::verifyState(const Window *wins, unsigned nwins,
                             bool bottomFirst) const
{
    Q_ASSERT(!dirtyState);

    // Query the window tree if necessary.
    Window *winsQueried = NULL;
    if (!wins) {
        Window foo;
        if (!XQueryTree(dpy, root, &foo, &foo, &winsQueried, &nwins)) {
            qCritical("MRestacker: XQueryTree() failed");
            return false;
        }
        Q_ASSERT(!winsQueried == !nwins);
        wins = winsQueried;
        Q_ASSERT(bottomFirst);
    }

    // Duplicate @state and trash it piece by piece.
    OrderedWindowStack state(this->state);
    for (unsigned o = 0; o < nwins; o++) {
        unsigned i = bottomFirst ? o : nwins-1-o;
        Q_ASSERT(wins[i] != None);
        if (!state.contains(wins[i])) {
            qCritical("MRestacker: 0x%lx is not in the state", wins[i]);
            return false;
        }

        // If the window is top/bottom check @sentinel.
        if (o == 0 && sentinel.below != wins[i]) {
            qCritical("MRestacker: bottom window is not 0x%lx, but 0x%lx",
                       wins[i], sentinel.below);
            return false;
        }
        if (o == nwins-1 && sentinel.above != wins[i]) {
            qCritical("MRestacker: top window is not 0x%lx, but 0x%lx",
                       wins[i], sentinel.above);
            return false;
        }

        // Figure out @expectedOrder.
        WindowOrder expectedOrder;
        Window lhs = i   > 0     ? wins[i-1] : None;
        Window rhs = i+1 < nwins ? wins[i+1] : None;
        if (bottomFirst) {
            expectedOrder.below = lhs;
            expectedOrder.above = rhs;
        } else {
            expectedOrder.above = lhs;
            expectedOrder.below = rhs;
        }

        // Check whether @order == @expectedOrder.
        WindowOrder order = state.take(wins[i]);
        if (order.above != expectedOrder.above) {
            qCritical("MRestacker: "
                      "above %u. window 0x%lx is not 0x%lx, but 0x%lx",
                       o, wins[i], expectedOrder.above, order.above);
            return false;
        } else if (order.below != expectedOrder.below) {
            qCritical("MRestacker: "
                      "below %u. window 0x%lx is not 0x%lx, but 0x%lx",
                       o, wins[i], expectedOrder.below, order.below);
            return false;
        }
    }

    // @nwins == 0 <=> @sentinel == None
    if (nwins > 0)
        Q_ASSERT(sentinel.above && sentinel.below);
    else if (sentinel.above || sentinel.below) {
        qCritical("MRestacker: there shouldn't be a sentinel, "
                  "but top=0x%lx, bottom=0x%lx",
                   sentinel.above, sentinel.below);
        return false;
    }

    // Are there windows in @state not in @wins?
    if (!state.isEmpty()) {
        qCritical("MRestacker: unexpected windows in state");
        return false;
    }

    if (winsQueried)
        XFree(winsQueried);
    return true;
}

// Insert @between BELOW @above, or at the top.  @between should be a new
// to @state.
void MRestacker::windowCreated(Window between, Window above)
{
    if (((MCompositeManager*)qApp)->ignoreThisWindow(between))
        return;
    if (!between || between == above) {
        qCritical("MRestacker: attempt to add window 0x%lx below 0x%lx",
                   between, above);
        return;
    }

    int nwins = state.count();
    WindowOrder &betweenOrder = state[between];
    if (nwins == state.count())
        // @win wasn't a new window in @state.
        qWarning("MRestacker: adding an existing window 0x%lx", between);

    OrderedWindowStack::iterator aboveOrder;
    if (above && (aboveOrder = state.find(above)) == state.end()) {
        qCritical("MRestacker: attempt to insert window below unknown 0x%lx",
                   above);
        above = None;
    }

    // Handle special cases.
    if (state.count() == 1) {
        // First window in @state.
        Q_ASSERT(!above && !betweenOrder.above && !betweenOrder.below);
        sentinel.above = sentinel.below = between;
        return;
    } else if (betweenOrder.above == above
               && (above || sentinel.above == between))
        // NOP, @between is already in place.
        return;

    if (betweenOrder.above)
        state[betweenOrder.above].below = betweenOrder.below;
    if (betweenOrder.below)
        state[betweenOrder.below].above = betweenOrder.above;
    else if (sentinel.below == between)
        sentinel.below = betweenOrder.above;

    if (above != None) {
        Q_ASSERT(aboveOrder != state.end());
        if (sentinel.above == between)
            sentinel.above = betweenOrder.below;
        betweenOrder.below  = aboveOrder->below;
        aboveOrder->below   = between;
        betweenOrder.above  = above;
    } else {
        betweenOrder.above  = None;
        betweenOrder.below  = sentinel.above;
        sentinel.above      = between;
    }

    if (betweenOrder.below)
        state[betweenOrder.below].above = between;
    else
        sentinel.below = between;
}

// Insert @between ABOVE @below, or at the bottom.  @between should be already
// in @state.
void MRestacker::windowConfigured(Window between, Window below)
{
    if (!between || between == below) {
        qCritical("MRestacker: attempt to move window 0x%lx above 0x%lx",
                   between, below);
        return;
    }

    int nwins = state.count();
    WindowOrder &betweenOrder = state[between];
    if (nwins < state.count())
        // @win wasn't in @state.
        qWarning("MRestacker: configuring a non-existing window 0x%lx",
                  between);

    OrderedWindowStack::iterator belowOrder;
    if (below && (belowOrder = state.find(below)) == state.end()) {
        qCritical("MRestacker: attempt to insert window above unknown 0x%lx",
                   below);
        below = None;
    }

    // Handle special cases.
    if (state.count() == 1) {
        // First window in @state.
        Q_ASSERT(!below && !betweenOrder.above && !betweenOrder.below);
        sentinel.above = sentinel.below = between;
        return;
    } else if (betweenOrder.below == below
               && (below || sentinel.below == between))
        // NOP, @between is already in place.
        return;

    if (betweenOrder.below)
        state[betweenOrder.below].above = betweenOrder.above;
    if (betweenOrder.above)
        state[betweenOrder.above].below = betweenOrder.below;
    else if (sentinel.above == between)
        sentinel.above = betweenOrder.below;

    if (below != None) {
        Q_ASSERT(belowOrder != state.end());
        if (sentinel.below == between)
            sentinel.below = betweenOrder.above;
        betweenOrder.above  = belowOrder->above;
        belowOrder->above   = between;
        betweenOrder.below  = below;
    } else {
        betweenOrder.below  = None;
        betweenOrder.above  = sentinel.below;
        sentinel.below      = between;
    }

    if (betweenOrder.above)
        state[betweenOrder.above].below = between;
    else
        sentinel.above = between;
}

// Remove @win from the @state, keeping the relative order
// of the remaining windows.
void MRestacker::windowDestroyed(Window win)
{
    OrderedWindowStack::iterator between = state.find(win);
    if (between == state.end()) {
        qWarning("MRestacker: attempt to remove "
                 "non-existing window 0x%lx", win);
        return;
    }
    Q_ASSERT(win != None);

    // Update the siblings of @win.
    if (between->above != None)
        state[between->above].below = between->below;
    else
        sentinel.above = between->below;

    if (between->below != None)
        state[between->below].above = between->above;
    else
        sentinel.below = between->above;

    state.erase(between);
}

// Examine an X window event and update the internal @state accordingly.
// Returns whether the event was interesting.
bool MRestacker::event(const XEvent *xev)
{
    // If we have syncState()d, this event may be obsolete.
    if (xev->xany.serial < skipToEvent)
        return false;
    skipToEvent = 0;

    // Are we interested in this event?
    if (xev->xany.window != root)
        return false;

    if (xev->type == CreateNotify)
        // Windows are created on the top of the stack.
        windowCreated(xev->xcreatewindow.window);
    else if (xev->type == DestroyNotify)
        windowDestroyed(xev->xdestroywindow.window);
    else if (xev->type == ConfigureNotify)
        // XConfigureNotify::above is the window's new below-sibling.
        windowConfigured(xev->xconfigure.window, xev->xconfigure.above);
    else if (xev->type == ReparentNotify) {
        if (xev->xreparent.parent != root)
            // Reparented from @root.
            windowDestroyed(xev->xreparent.window);
        else if (!state.contains(xev->xreparent.window))
            // Newly reparented to @root.
            windowCreated(xev->xreparent.window);
    } else // Wasn't so interesting after all.
        return false;

    return true;
}

// XCheckIfEvent() callback
typedef struct {
    MRestacker *mrs;
    WindowStack *stack;
} ProcessPendingEventsArgs;

static Bool processDestroyNotify(Display *, XEvent *xev, XPointer xargs)
{
    const ProcessPendingEventsArgs *args;

    args = (ProcessPendingEventsArgs*)(xargs);
    if (args->mrs->event(xev) && args->stack && xev->type == DestroyNotify)
        args->stack->removeOne(xev->xdestroywindow.window);

    return False; // process all events in the input queue
}

// Update the internal @state according to the events pending
// in the input queue.  If @stack is not NULL, remove destroyed
// windows from it to prevent stacking errors.
void MRestacker::processDestroyNotifys(WindowStack *stack)
{
    ProcessPendingEventsArgs ppeargs = { this, stack };
    XCheckIfEvent(dpy, NULL, processDestroyNotify, (XPointer)&ppeargs);
    dirtyState = false;
}

/**
 * Manipulate window stack by inserting a window below some other
 * window. The window is assumed to have been isolated earlier by
 * @removeWindow.
 *
 * @param[in] between Window to be moved, can't be None.
 *
 * @param[in] above Window over moved window, can't be None.
 *
 * @param[out] oldWindows Changed window ordering.
 */
static void insertWindow(Window between, Window above,
                         OrderedWindowStack &oldWindows,
                         WindowOrder &bounds)
{
    Q_ASSERT(between && above && oldWindows.contains(above));
    OrderedWindowStack::iterator aboveOrder = oldWindows.find(above);

    Window below = (*aboveOrder).below;
    RESTACKER_LOG_IF(false, "  insert: %lx.below = %lx\n", aboveOrder.key(),
                     between);
    (*aboveOrder).below = between;
    if (below != None) {
        RESTACKER_LOG_IF(false, "  insert: %lx.above = %lx\n", below, between);
        oldWindows[below].above = between;
    }

    WindowOrder order = { above, below };
    RESTACKER_LOG_IF(false, "  insert: %lx = { above:%lx, below:%lx }\n",
                     between, above, below);
    oldWindows[between] = order;

    // Update @bounds if @between is a top or bottom window now.
    if (!below)
        bounds.below = between;
    if (!above)
        bounds.above = between;
}

/**
 * Isolate a window from a window stack. We don't really remove
 * it, we just float it out of the stack ordering temporarily.
 * The floating window needs to be added back with @insertWindow.
 *
 * @param[in] below Window under extracted window, None if at bottom.
 *
 * @param[in] between Window to be removed, can't be None.
 *
 * @param[in] above Window over extracted window, None if at top.
 *
 * @param[out] oldWindows Changed window ordering.
 */
static void removeWindow(Window below, Window between, Window above,
                         OrderedWindowStack &oldWindows,
                         WindowOrder &bounds)
{
    Q_ASSERT(between != None);

    if (below != None) {
        RESTACKER_LOG_IF(false, "  remove: %lx.above = %lx\n", below, above);
        oldWindows[below].above = above;
    }
    if (above != None) {
        RESTACKER_LOG_IF(false, "  remove: %lx.below = %lx\n", above, below);
        oldWindows[above].below = below;
    }

    WindowOrder order = { None, None };
    RESTACKER_LOG_IF(false, "  remove: %lx = { above:%lx, below:%lx }\n",
                     between, order.above, order.below);
    oldWindows[between] = order;

    // Update @bounds if @between was the top or bottom window.
    if (bounds.below == between)
        bounds.below = above;
    if (bounds.above == between)
        bounds.above = below;
}

#if 0
// print window stack, topmost last
static void dumpStack(const OrderedWindowStack &stack,
                      const WindowStack &compare)
{
    QString line;

    if (stack.empty())
        qDebug() << __func__ << "(empty stack)";

    Window i = stack.begin().key();

    while (stack[i].below != None)
        i = stack[i].below;

    int items = 0;
    while (i != None) {
        if (stack[i].below != None)
            line += ", ";
        if (compare.contains(i))
            line += QString().sprintf("%lx*", i);
        else
            line += QString().sprintf("%lx", i);
        ++items;
        i = stack[i].above;
    }

    qDebug() << __func__ << line << items;
}
#endif

static void naiveOpsFinder(const WindowStack &newStack,
                           const OrderedWindowStack &oldStack,
                           StackOps &stackOps)
{
    QList<Window> oldList;
    Window key = oldStack.begin().key();
    while (oldStack[key].below != None)
        key = oldStack[key].below;
    for (; key != None; key = oldStack[key].above)
        oldList.append(key);

    QSet<Window> movedSet; // used to avoid an infinite loop
    for (int i = 0; i < oldList.size();) {
        Window w = newStack.at(i);
        Window oldw = oldList.at(i);
        if (w != oldw && !movedSet.contains(oldw)) {
            int new_i = newStack.indexOf(oldw, i);
            oldList.move(i, new_i);
            movedSet.insert(oldw);
            WindowOrder order = { new_i < oldList.size() - 1 ?
                                  oldList.at(new_i + 1) : None, oldw };
            stackOps.push_back(order);
            continue;
        } else if (w != oldw) {
            int old_i = oldList.indexOf(w, i);
            oldList.move(old_i, i);
            WindowOrder order = { i < oldList.size() - 1 ?
                                  oldList.at(i + 1) : None, w };
            stackOps.push_back(order);
        }
        movedSet.clear();
        ++i;
    }
}

/**
 * Generate stacking operations required to get from @oldWindows
 * to @newWindows. Every element of @newWindows should be present
 * in @oldWindows.
 *
 * @param[in] newWindows Requested stacking order of windows.
 *
 * @param[in] checkRemovals Whether to be aggressive or conservative.
 *
 * @param[in,out] oldWindows Current stacking order of windows.
 *                           As a side effect the order is changed
 *                           so that @newWindows is contained as a
 *                           substack in @oldWindows.
 *
 * @param[in,out] bounds The top and bottom of @oldWindows.
 *
 * @param[out] stackOps Stacking operations to get from current
 *                      stacking order to preferred stacking order.
 */
static void generateStackOps(
                     const WindowStack &newWindows, const bool checkRemovals,
                     OrderedWindowStack &oldWindows, WindowOrder &bounds,
                     StackOps& stackOps)
{
    // Quickly determine whether a window has some property (belongs to a set).
    typedef QSet<Window> WindowSet;

    // Verify validity of parameters.
    Q_ASSERT(newWindows.count() > 1);
    Q_ASSERT(stackOps.isEmpty());

    // We collect windows in @oldWindows that aren't present in
    // @newWindows here. The windows in @bottomWindows are moved below
    // bottom window of @newWindows.
    WindowStack bottomWindows; // topmost is stored first exceptionally
    WindowStack::const_iterator iterator = newWindows.begin(); // first element
    Window bottomWindow = (*iterator); // window at the bottom

    // We collect here the windows that are present in both
    // @oldWindows and @newWindows but that will be restacked with a
    // delay.
    WindowSet pendingWindowSet;

    // We need to quickly determine whether @newWindows contains some
    // windows. Therefore we create a temporary set for that with the
    // exact contents of @newWindows. This set could be generated
    // outside of the function as a minor optimization.
    WindowSet newWindowSet;
    StackOps naiveOps;
    if (checkRemovals) {
        int same = 0;
        for (; iterator != newWindows.end(); ++iterator) {
            newWindowSet.insert(*iterator);
            if (oldWindows.contains(*iterator))
                ++same;
        }
        // check if few moves is all we need
        if (same == oldWindows.count() && same == newWindows.count())
            naiveOpsFinder(newWindows, oldWindows, naiveOps);
    } else
        iterator = newWindows.end();

    // Setup looping. We will loop through all consecutive pairs in
    // @newWindows from top to bottom.
    --iterator; // last element
    Window newAbove = (*iterator); // topmost window
    Q_ASSERT(oldWindows.contains(newAbove));
    OrderedWindowStack::const_iterator aboveOrder = oldWindows.find(newAbove);
    --iterator; // penultimate element
    bool forceFallback = false;

    // Generate required stacking operations based on the new window
    // stack. After each round through the loop, @oldWindows and
    // @newWindows are in the same order up to that point. After the
    // loop, every common element found in both @oldWindows and
    // @newWindows are identically ordered. Thus @stackOps contains
    // the set of changes to get from @oldWindows to @newWindows.
    int i = newWindows.size() - 2; // i matches the index of iterator
    while (i >= 0) {
        Window newBetween = (*iterator);
        OrderedWindowStack::const_iterator betweenOrder = oldWindows.find(newBetween);
        Q_ASSERT(betweenOrder != oldWindows.end());

        // Check if the stacks are already locally in the same
        // order. In that case window x has the same windows on top.
        // @oldWindows = [ a b x c d e ]
        // @newWindows = [ a b x d c ]
        Window oldAbove = (*betweenOrder).above;
        if (oldAbove != newAbove) {
            // Check if more aggressive approach should be used.
            Window oldBetween = (*aboveOrder).below;
            if (checkRemovals && (oldBetween != None)
                && !pendingWindowSet.contains(newBetween)) {
                // The window is moved out of the way temporarily. The
                // stacking operations reflecting this change are
                // generated later or as the last step of the
                // function.
                Window oldBelow = oldWindows[oldBetween].below;
                // Next we'll check if @oldWindows contains some extra
                // windows that aren't present in @newWindows.
                Q_ASSERT(!newWindowSet.isEmpty());
                if (!newWindowSet.contains(oldBetween)) {
                    // Move x directly to the bottom of @newWindows.
                    // @oldWindows = [ a b x c d e ]
                    // @newWindows = [ a b c d e ]
                    removeWindow(oldBelow, oldBetween, newAbove,
                                 oldWindows, bounds);
                    bottomWindows.push_back(oldBetween);
                    // Delay the loop for one step.
                    continue;
                } else if (!forceFallback) {
                    // Move x directly to its correct place later.
                    // @oldWindows = [ a b x c d e ]
                    // @newWindows = [ a b c d x e ]
                    removeWindow(oldBelow, oldBetween, newAbove,
                                 oldWindows, bounds);
                    pendingWindowSet.insert(oldBetween);
                    // Don't repeat this case too many times in a row.
                    // @oldWindows = [ a b x c d e ]
                    // @newWindows = [ a b e d c x ]
                    forceFallback = true;
                    // Delay the loop for one step.
                    continue;
                }
            }

            // Create new stacking operation, because the stacks
            // aren't in the same order.
            // @oldWindows [ a b c d x ]
            // @newWindows [ a b x c d ]
            WindowOrder order = { newAbove, newBetween };
            stackOps.push_back(order);
            // Transform old window ordering to include the stacking
            // operation.
            Window oldBelow = (*betweenOrder).below;
            // Removal is a NOP if @newBetween is in
            // @pendingWindows. We don't bother to clean
            // @pendingWindows.
            removeWindow(oldBelow, newBetween, oldAbove, oldWindows, bounds);
            insertWindow(newBetween, newAbove, oldWindows, bounds);
        }
        // Advance the loop for one step.
        newAbove = newBetween;
        aboveOrder = betweenOrder;
        forceFallback = false;
        --iterator;
        --i;
    }

    // Reordering was successful. Generate remaining stacking
    // operations by moving removed windows at the bottom of the
    // stack.
    Q_ASSERT(bottomWindows.isEmpty() || newWindows != oldWindows.keys());
    for (WindowStack::const_iterator it = bottomWindows.begin();
         it != bottomWindows.end(); ++it) {
        Window window = *it;
        WindowOrder order = { bottomWindow, window };
        stackOps.push_back(order);
        insertWindow(window, bottomWindow, oldWindows, bounds);
        bottomWindow = window;
    }
    if (!naiveOps.isEmpty() && naiveOps.count() < stackOps.count())
        stackOps = naiveOps;
}

// Get the %StackOps to achieve @newOrder from the current @state,
// and update it accordingly.
StackOps MRestacker::plan(WindowStack &newOrder,
                          WindowOrder &newBounds,
                          OrderedWindowStack &newState)

{
    // With an outdated @state our plan could be bullshit.
    Q_ASSERT(!dirtyState);
    Q_ASSERT(&newBounds != &sentinel && &newState != &state);

    // Filter out the @newOrder windows not in @state, as generateStackOps()
    // can't do anything about them.
    WindowStack::iterator it = newOrder.begin();
    while (it != newOrder.end())
        if (!state.contains(*it)) {
#ifndef WINDOW_DEBUG
            qWarning("MRestacker: ignoring unknown window 0x%lx", *it);
#endif
            it = newOrder.erase(it);
        } else
            ++it;

    // Check if there is nothing to be done.
    if (newOrder.count() < 2)
        return StackOps();

    // Try aggressively first.
    StackOps stackOps;
    PlannerStatistics *stats = &altStats;
    generateStackOps(newOrder, true, newState, newBounds, stackOps);

    // Can it be improved?
    if (stackOps.count() > 1) {
        StackOps conStackOps;
        WindowOrder conBounds(sentinel);
        OrderedWindowStack conState(state);

        generateStackOps(newOrder, false, conState, conBounds, conStackOps);
        Q_ASSERT(conState  == newState);
        Q_ASSERT(conBounds == newBounds);

        // Choose the one that results in less operations.
        if (stackOps.count() > conStackOps.count()) {
            stackOps = conStackOps;
            stats = &conStats;
        }
    }

    // Check that @newState is reasonable.
    if (stackOps.isEmpty()) {
        Q_ASSERT(newState  == state);
        Q_ASSERT(newBounds == sentinel);
    } else
        Q_ASSERT(newState != state);

    // Update planner statistics.
    stats->nplans++;
    stats->nwindows  += newOrder.count();
    stats->nstackops += stackOps.count();
    stats->duties    += (qreal)stackOps.count() / newOrder.count();

    return stackOps;
}

// XSetErrorHandler() callback
static bool restackError;
static QList<unsigned long> pendingRequests;

static int newErrorHandler(Display *, XErrorEvent *error)
{   // If we've got an error don't expect a reply.
    restackError |= error->request_code == X_ConfigureWindow;
    pendingRequests.removeOne(error->serial);
    return 0;
}

// Send @ops to X and return whether they caused any error.
// The connection is assumed to have been XSync()ed.
bool MRestacker::execute(const StackOps &ops)
{
    // Error handler used with X requests.
    typedef int (*xErrorHandler)(Display *, XErrorEvent *);

    // Verify validity of parameters
    Q_ASSERT(dpy != NULL);
    Q_ASSERT(!dirtyState);

    // Check if there is nothing to be done.
    if (ops.isEmpty())
        return true;

    // @pendingRequests will contain the successful requests.
    restackError = false;
    xErrorHandler oldErrorHandler = XSetErrorHandler(newErrorHandler);
    pendingRequests.clear();

    // Send the requests.
    XWindowChanges changes;
    foreach (WindowOrder const &op, ops) {
        Q_ASSERT(op.above && op.below);
        int mask = CWStackMode;
        if (op.above == None) {
            changes.stack_mode = Above;
        } else {
            changes.stack_mode = Below;
            changes.sibling = op.above;
            mask |= CWSibling;
        }
        pendingRequests.append(NextRequest(dpy));
        XConfigureWindow(dpy, op.below, mask, &changes);
    }

    // Catch the errors.
    XSync(dpy, False);
    XSetErrorHandler(oldErrorHandler);

    dirtyState = restackError;
    return !restackError;
}

/**
 * Restack the windows to match the given order. The preferred window stack
 * in @newOrder is reversed and an algorithm equivalent to XRestackWindows
 * is run on it.
 *
 * @param[in] newOrder Preferred window stack, topmost last.
 *                       Unknown windows, which are not in @state,
 *                       are grumpily ignored.
 *
 * @return True on success, false if restacking was not completely succesful.
 *         Errors may happen when @newOrder contains bad windows.
 */
bool MRestacker::restack(WindowStack newOrder)
{
    WindowOrder oldBounds, newBounds;
    OrderedWindowStack oldState, newState;

    if (dpy) {
        // In order to reduce the chance of failures (which eventually
        // lead to retries) and prevent unnecessary warnings, peek into
        // the input queue to learn as much about the real current
        // stacking as possible.
        bool okay;
        unsigned long oldSkip;

        // @newState <- @state + processDestroyNotifys()
        oldBounds   = sentinel;
        oldState    = state;
        oldSkip     = skipToEvent;
        XSync(dpy, False);
        processDestroyNotifys(&newOrder);

        // Feed plan() with @newState but keep @oldState,
        // and let the caller update us explicitly with event()s.
        newBounds   = sentinel;
        newState    = state;
        okay = execute(plan(newOrder, newBounds, newState));

        // Restore @oldState.
        sentinel    = oldBounds;
        state       = oldState;
        skipToEvent = oldSkip;

        return okay;
    } else {
        // In mock mode we aren't fed with event()s, so we have
        // to keep ourselves up to date.
        newBounds   = sentinel;
        newState    = state;
        plan(newOrder, newBounds, newState);
        sentinel    = newBounds;
        state       = newState;

        return true;
    }
}
