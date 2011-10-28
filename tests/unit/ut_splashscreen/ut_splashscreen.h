#ifndef UT_SPLASHSCREEN_H
#define UT_SPLASHSCREEN_H

#include <QtTest/QtTest>
#include <QVector>
#include "mcompositemanager.h"

class fake_LMT_window;

class ut_splashscreen : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void init();
    void cleanup();

    void showSplashScreen();
    void testFade();
    void testIconifyInMapRequestEvent();
    void testTimerStartedInMapEvent();
    void testTimerStartingWhenDismissedSplashScreenIsStackedToBottom();
    void testStackingSplashBelow_data();
    void testStackingSplashBelow();
    void testSplashDismissalWhenSettingUpCrossFade();
    void testFadingUnmappedWindow();
    void testConfigureWindow();
    void testNetActiveWindowMessage_data();
    void testNetActiveWindowMessage();
    void testCloseHandler();
    void testDismissedSplash();
    void testDestroyedSplash();
    void testSplashOrientation();
    void cleanupTestCase();

private:
    void requestSplash(const QString& pid, const QString& wmClass,
          const QString& pPixmap, const QString& lPixmap,
          const QString &pixmapId);

    void addWindow(MWindowPropertyCache *pc);
    void mapWindow(MWindowPropertyCache *pc);
    void verifyDisabledComposition(MCompositeWindow *cw);
    void testOrientation(const char *topedge, bool is_flat, unsigned expected);

    MCompositeManager *cmgr;
    QPixmap splashPixmap;
    fake_LMT_window *pc;
    Window w;

private slots:
    void onAnimationsFinished(MCompositeWindow *cw);
};

#endif
