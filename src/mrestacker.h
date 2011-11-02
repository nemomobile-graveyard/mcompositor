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

#ifndef MRESTACKER_H
#define MRESTACKER_H

#include <QVector>
#include <QList>
#include <QHash>
#include <X11/Xlib.h>

/**
 * An algorithm for restacking windows.
 *
 * The user gives a preferred window stack to the algorithm. The
 * algorithm reads the current window stack and starts to compare it
 * with the preferred window stack. Restacking operations are
 * generated whenever a new transformation is needed to get from
 * current window stack to preferred window stack. The restacking
 * operations are then send to X server as ConfigureWindow requests.
 *
 * Running this algorithm is meant to be a replacement for calling
 * XRestackWindows. Unit tests compare the window ordering after
 * running this algorithm to the result of XRestackWindows. However,
 * the identical goal may be achieved by doing different
 * ConfigureWindow requests in a different order.
 */
class MRestacker
{
public:
    // Stacking order of windows, topmost last.
    typedef QList<Window> WindowStack;
    // Order of a single window in window stack.
    typedef struct {
        Window above; // None if topmost.
        Window below; // None if at bottom.
    } WindowOrder;
    // Ordered fast access window stack.
    typedef QHash<Window, WindowOrder> OrderedWindowStack;
    // Restacking operation: move window @below under window @above. */
    typedef QVector<WindowOrder> StackOps;
    typedef struct PlannerStatistics {
        unsigned nplans;    // number of times the planner won
        unsigned nwindows;  // how many windows did it stack
        unsigned nstackops; // with how many operations
        qreal    duties;    // sum of @nstackops/@nwindows for each plan
        QString toString() const;
    } PlannerStatistics;

public:
    MRestacker()                            { init(); }
    MRestacker(Display *dpy, Window root)   { init(dpy, root); }
    void init(Display *dpy=NULL, Window root=None);
    void resetStats();

    void setState(const Window *wins, unsigned nwins, bool bottomFirst=true,
                  const OrderedWindowStack *subset=NULL);
    void setState(const QVector<Window> &wins, bool bottomFirst=true,
                  const OrderedWindowStack *subset=NULL)
        { setState(wins.constData(), wins.count(), bottomFirst, subset); }
    void setState(const MRestacker &other);
    bool syncState(bool allChildren=false);

    WindowStack getState() const;
    bool verifyState(const Window *wins=NULL, unsigned nwins=0,
                     bool bottomFirst=true) const;
    bool verifyState(const QVector<Window> &wins,
                     bool bottomFirst=true) const
        { return verifyState(wins.constData(), wins.count(), bottomFirst); }

    void windowCreated(Window win, Window above=None);
    void windowConfigured(Window win, Window above=None);
    void windowDestroyed(Window win);

    bool event(const XEvent *xev);

    bool restack(WindowStack newOrder);

public:
    // Conservative/aggressive planner statistics.
    PlannerStatistics conStats, altStats;

private:
    void processDestroyNotifys(WindowStack *stack=NULL);
    StackOps plan(WindowStack &newOrder,
                  WindowOrder &newBounds, OrderedWindowStack &newState);
    bool execute(const StackOps &ops);

    Display *dpy;
    Window root;
    WindowOrder sentinel;       // top and bottom windows of @state
    OrderedWindowStack state;   // the stacking to which the next plan() apply
    bool dirtyState;            // whether we believe @state is up to date
    unsigned long skipToEvent;  // Set by syncState() to tell event() up to
                                // when it should consider events obsolete.
};

#endif // MRESTRACKER_H
