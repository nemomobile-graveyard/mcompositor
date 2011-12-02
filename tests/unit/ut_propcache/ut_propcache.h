#ifndef UT_PROPCACHE_H
#define UT_PROPCACHE_H

#include <QtTest/QtTest>
#include <QVector>
#include "mcompositemanager.h"

class ut_PropCache : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void testIsDecorator();
    void testTransientFor();
    void testInvokedBy();
    void testMeegoStackingLayer();
    void testLowPowerMode();
    void testOpaqueWindow();
    void testOrientationAngle();
    void testAlwaysMapped();
    void testCannotMinimize();
    void testPid();
    void testGlobalAlpha();
    void testVideoGlobalAlpha();
    void testNoAnimations();
    void testVideoOverlay();
    void testWindowTypes();

private:
    void testVoidArgFunc(const char *fname, const char *rtype, Atom atom,
                         int rsize = 4,
                         unsigned set_val = 1, unsigned expected_set_val = 1,
                         unsigned expected_unset_val = 0);
    void propertyEvent(Atom atom, Window window, int state);
    MCompositeManager *cmgr;
};

#endif
