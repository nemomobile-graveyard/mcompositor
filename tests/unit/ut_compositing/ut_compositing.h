#ifndef UT_COMPOSITING_H
#define UT_COMPOSITING_H

#include <QtTest/QtTest>
#include <QVector>
#include "mcompositemanager.h"

class ut_Compositing : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void testDesktopMapping();  
    void testAppMapping();  
    void testAppUnmapping();  
    void testAppRemapping();
    void testVkbMappingWhenAppAnimating();
    void testVkbMapping();
    void testBannerMapping();
    void testBannerUnmapping();
    void testDamageDuringDisplayOff();
    void testDamageDuringTransparentMenu();
    void testDamageToObscuredRGBAWindow();
    void testDamageToObscuredSmallWindow();

private:
    MCompositeManager *cmgr;
    void prepareStack(QVector<MWindowPropertyCache *> &t);
    void mapWindow(MWindowPropertyCache *pc);  
    void unmapWindow(MWindowPropertyCache *pc);  
    void fakeDamageEvent(MCompositeWindow *cw);
};

#endif
