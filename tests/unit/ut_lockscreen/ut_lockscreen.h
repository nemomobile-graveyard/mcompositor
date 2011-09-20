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
    void testScreenOffAndThenQuicklyOn();
    void testPaintingDuringScreenOff();

private:
    void mapWindow(MWindowPropertyCache *pc);
    void unmapLockscreen();
    void fakeDamageEvent(MCompositeWindow *cw);
    MCompositeManager *cmgr;
};

#endif
