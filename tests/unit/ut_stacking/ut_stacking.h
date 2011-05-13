#ifndef UT_STACKING_H
#define UT_STACKING_H

#include <QtTest/QtTest>
#include <QVector>
#include "mcompositemanager.h"

class ut_Stacking : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void testLotOfUnmapped();  
    void testRaisingAndMapping();  
    void testBehind();

private:
    MCompositeManager *cmgr;
    bool checkOrder(Window t[]);
    void prepareStack(QVector<MWindowPropertyCache *> &t);
};

#endif
