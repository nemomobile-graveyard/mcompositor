/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (directui@nokia.com)
**
** This file is part of duicompositor.
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

#include <MApplication>
#include <mcomponentdata.h>
#include "mondisplaychangeevent.h"
#include "mdecoratorwindow.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xmd.h>

class MDecoratorApp : public MApplication
{
public:
    MDecoratorApp(int argc, char **argv) : MApplication(argc, argv)
    {
        window.show();
    }

    virtual bool x11EventFilter(XEvent *xev)
    {
        if (xev->type == VisibilityNotify) {
            XVisibilityEvent *xve = (XVisibilityEvent *)xev;

            foreach (MWindow *win, MComponentData::instance()->windows()) {
                if (win && win->effectiveWinId() == xve->window) {
                    MOnDisplayChangeEvent mev(
                        xve->state != VisibilityFullyObscured
                            ? MOnDisplayChangeEvent::FullyOnDisplay
                            : MOnDisplayChangeEvent::FullyOffDisplay,
                        QRectF(QPointF(0, 0), win->visibleSceneSize()));
                    sendEvent(win, &mev);
                }
            }
        }

        return MApplication::x11EventFilter(xev);
    }

private:
    MDecoratorWindow window;
};

int main(int argc, char **argv)
{
    MDecoratorApp app(argc, argv);

    return app.exec();
}
