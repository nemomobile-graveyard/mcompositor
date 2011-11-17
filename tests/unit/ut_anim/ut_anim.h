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
    void testIconifyingAnimationBelowLockscreen();
    void testRestoreAnimation();
    void testCloseAnimation();
    void testStartupAnimForSecondTimeMapped();
    void testNoAnimations();  
    void testSkipAnimationsWhenMeegoLevelWindowIsMapped();
    void testSkipAnimationsWhenSystemModalIsMapped();
    void testDontSkipAnimationsWhenHigherMeegoLevelWindowIsMapped();

    void testOpenChainingAnimation();
    void testCloseChainingAnimation();

    void testOpenSheetAnimation();
    void testCloseSheetAnimation();

    void testDerivedAnimHandler();
    void testExternalAnimHandler();

    void testGrabberRace();

private:
    void fakeDamageEvent(MCompositeWindow *cw);
    void addWindow(MWindowPropertyCache *pc);
    void mapWindow(MWindowPropertyCache *pc);
    MCompositeManager *cmgr;
};

#endif
