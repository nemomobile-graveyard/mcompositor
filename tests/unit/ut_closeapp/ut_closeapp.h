#ifndef UT_CLOSEAPP_H
#define UT_CLOSEAPP_H

#include <QtTest/QtTest>
#include <QVector>
#include "mcompositemanager.h"

class ut_CloseApp : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void testKillIconified();
    void testCloseIconifiedWithRaise();

private:
    MCompositeManager *cmgr;
    void prepareStack(QVector<MWindowPropertyCache *> &t);
};

#endif
