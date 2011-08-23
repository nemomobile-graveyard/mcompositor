#ifndef UT_NETCLIENTLIST_H
#define UT_NETCLIENTLIST_H

#include <QtTest/QtTest>
#include <QVector>
#include "mcompositemanager.h"

class ut_netClientList : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void testDesktopMapping();  
    void testAppMapping();  
    void testAppMapping2();  
    void testAppUnmapping();  
    void testORMapping();  
    void testDecoratorMapping();  
    void testDockMapping();  
    void testVirtualMapping();  

private:
    MCompositeManager *cmgr;
    void prepareStack(QVector<MWindowPropertyCache *> &t);
    void mapWindow(MWindowPropertyCache *pc);  
    void unmapWindow(MWindowPropertyCache *pc);  
};

#endif
