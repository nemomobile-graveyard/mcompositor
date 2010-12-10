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

#include <QTextStream>
#include <MApplication>
#include "mdecoratorwindow.h"

#ifdef MDECORATOR_DEBUG
#include <QFile>

QFile* file = 0;

void myMessageOutput(QtMsgType type, const char *msg)
{
    if(!file) {
        file = new QFile("/tmp/mdecorator.log");
        file->open(QIODevice::Text | QIODevice::WriteOnly);
    }
    QTextStream stream(file);

    switch (type) {
     case QtDebugMsg:
         stream << "Debug: "<< msg<<"\n";
         break;
     case QtWarningMsg:
         stream << "Warning: "<< msg<<"\n";
         break;
     case QtCriticalMsg:
         stream << "Critical: "<< msg<<"\n";
         break;
     case QtFatalMsg:
         stream << "Fatal: "<< msg<<"\n";
         abort();
    }
}
#endif

class MDecoratorApp : public MApplication
{
public:
    MDecoratorApp(int argc, char **argv) : MApplication(argc, argv)
    {
        window.show();
    }

    virtual bool x11EventFilter(XEvent *e)
    {
        bool ret = window.x11Event(e);
        if (!ret)
            ret = MApplication::x11EventFilter(e);
        return ret;
    }

private:
    MDecoratorWindow window;
};

int main(int argc, char **argv)
{
#ifdef MDECORATOR_DEBUG
    qInstallMsgHandler(myMessageOutput);
#endif
    MDecoratorApp app(argc, argv);

    return app.exec();
}
