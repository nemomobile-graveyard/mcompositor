#ifndef UT_LOCKSCREEN_H
#define UT_LOCKSCREEN_H

#include <QtTest/QtTest>
#include <QVector>
#include "mcompositemanager.h"

class ut_Lockscreen : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void testScreenOnBeforeLockscreenPaint();  
    void testScreenOnAfterLockscreenPaint();  
    void testScreenOnAfterMapButBeforePaint();
    void testScreenOnThenMapsButDoesNotPaint();
    void testScreenOnButLockscreenTimesOut();  
    void testScreenOnAndThenQuicklyOff();  

private:
    MCompositeManager *cmgr;
};

#endif
