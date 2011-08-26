#ifndef UT_ANIM_H
#define UT_ANIM_H

#include <QtTest/QtTest>
#include <QVector>
#include "mcompositemanager.h"

class ut_Anim : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void testDamageTimeout();
    void testStartupAnimForFirstTimeMapped();
    void testIconifyingAnimation();
    void testRestoreAnimation();
    void testCloseAnimation();
    void testStartupAnimForSecondTimeMapped();
    void testNoAnimations();  

    void testOpenChainingAnimation();
    void testCloseChainingAnimation();

    void testDerivedAnimHandler();
    void testExternalAnimHandler();

private:
    void addWindow(MWindowPropertyCache *pc);
    MCompositeManager *cmgr;
};

#endif
