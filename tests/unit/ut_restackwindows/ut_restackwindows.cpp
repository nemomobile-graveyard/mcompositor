/* A test that checks if the optimized restack windows implementation
   results in windows being ordered into the same order as
   XRestackWindows would do */
#include <QTest>
#include <QVector>
#include <QDebug>

#include "mcompositemanager.h"
#include "mrestacker.h"
#include "stats.h"

#include <vector>
#include <algorithm>
#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>
#include <X11/Xlib.h>

// Type definitions
typedef Window Window;
typedef QVector<Window> WindowVec;

class SuperStackerTest: public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void testExhaustiveComparative();
    void testExhaustiveStandalone();
    void testRotationsComparative();
    void testRotationsStandalone();
    void testPresetComparative();
    void testPresetStandalone();
    void testRandomComparative();
    void testRandomStandalone();
    void testState();
    void testStateRandom();
};

#include "moc_ut_restackwindows.cpp"

// Private variables
// @managedWindows holds all the windows in reference order we may care
// to restack in our universe, contained by @fakeRoot.
static Display *Dpy;
static Window fakeRoot;
static WindowVec managedWindows;
static MRestacker testRestacker;

// @optHeadless:        whether to test with X
// @optNWindows:        the number of @managedWindows to have
// @optExhaustiveCut:   as limit of your patience, and determines
//                      at most how many windows' restacking should
//                      exhaustiveTest() do
// @optLearning:        whether to produce or verify planner statistics
// @optNRandomTests:    how many iterations should the random tests do
static bool     optHeadless      = false;
static int      optNWindows      = 15;      // -nwindows
static int      optExhaustiveCut =  6;      // -maxwindows
static bool     optLearning      = false;   // -learn
static unsigned optNRandomTests  = 0;       // -nrandom

// Program code
// Implement the stdc++ %RandomNumberGenerator interface, which we can pass 
// to std::vector functions, and know how to seed.
static int libcRandom(int max)
{   // rand(3) says the less significant bits of random(3) are random too
    return random() % max;
}

// Returns how many different ordered subsets of an @n-element set has,
// ie. how many different ways can you stack a set of windows.
static unsigned numberOfSubsets(unsigned n)
{   // \sum{i=1}{n}{\choose{n}{i}\perm{i}}
    unsigned p, sum;
    for (sum = 0, p = 1; n > 0; n--) {
        p   *= n;
        sum += p;
    }
    // An empty set of windows can be stacked a single way.
    return sum ? sum : 1;
}

// %PlannerStatistics == %PlannerStatistics
static bool operator==(MRestacker::PlannerStatistics const &lhs,
                       MRestacker::PlannerStatistics const &rhs)
{
    return lhs.nplans    == rhs.nplans
        && lhs.nwindows  == rhs.nwindows
        && lhs.nstackops == rhs.nstackops
        && qFuzzyCompare(lhs.duties, rhs.duties);
}

// %PlannerStatistics - %PlannerStatistics
static MRestacker::PlannerStatistics operator-(
                               MRestacker::PlannerStatistics const &lhs,
                               MRestacker::PlannerStatistics const &rhs)
{
    MRestacker::PlannerStatistics diff;
    diff.nplans     = lhs.nplans-rhs.nplans;
    diff.nwindows   = lhs.nwindows-rhs.nwindows;
    diff.nstackops  = lhs.nstackops-rhs.nstackops;
    diff.duties     = lhs.duties-rhs.duties;
    return diff;
}

template<class T>
static __attribute__((unused))
QDebug &printWins(QDebug &out, T const &wins)
{
    out << "[";
    foreach (Window win, wins)
        out << QString().sprintf("0x%lx", win).toLatin1().constData();
    out << "]";
    return out;
}

// Print statistics after each test function.
static void printStats()
{
    qDebug("\n         conservative planner: %s"
           "\n         aggressive planner:   %s",
            testRestacker.conStats.toString().toLatin1().constData(),
            testRestacker.altStats.toString().toLatin1().constData());
}

// If we have learnt statistics, verify that the statistics have changed
// as much as expected, or in @optLearning mode, print the newly learnt
// statistics.
static void verifyStats(const LearntStatsTbl *tbl, int idx)
{   // Previous statistics; used to get the difference.
    static MRestacker::PlannerStatistics con, alt;

    if (!tbl) { // reset
        memset(&con, 0, sizeof(con));
        memset(&alt, 0, sizeof(alt));
        return;
    }

    // Determine the difference from the previous statistics.
    MRestacker::PlannerStatistics conDiff = testRestacker.conStats - con;
    MRestacker::PlannerStatistics altDiff = testRestacker.altStats - alt;
    con = testRestacker.conStats;
    alt = testRestacker.altStats;

    // Verify or output the difference.
    if (optLearning) {
        qDebug("\n stats : /* %u */ "
               "{ { %u, %u, %u, %f }, { %u, %u, %u, %f } },",
               idx,
               conDiff.nplans, conDiff.nwindows, conDiff.nstackops,
               conDiff.duties,
               altDiff.nplans, altDiff.nwindows, altDiff.nstackops,
               altDiff.duties);
    } else if (idx < tbl->nentries) {
        QCOMPARE(tbl->data[idx].conStats, conDiff);
        QCOMPARE(tbl->data[idx].altStats, altDiff);
    }
}

// Verifies that the children present in @children are in the order of @order.
template<class T1, class T2>
static bool verifyOrder(const T1 &order,
                        const T2 &children, unsigned nchildren)
{
    int o = 0;
    for (unsigned i = 0; o < order.count(); i++)
        if (i >= nchildren) {
            // out of @children
            qWarning("children out of order");
            return false;
        } else if (children[i] == order[o])
            o++;
    return true;
}

// Verifies that the children of @fakeRoot persent in @order are in order.
// If @childrenp is not %NULL the list of children is stored there.
template<class T>
static bool verifyTree(const T &order,
                       Window **childrenp, unsigned *nchildrenp)
{
    Window foo;
    Window *children;
    unsigned nchildren;

    Q_ASSERT(Dpy != NULL);
    if (!XQueryTree(Dpy, fakeRoot, &foo, &foo, &children, &nchildren))
        qFatal("XQueryTree() failed");

    if (!verifyOrder(order, children, nchildren))
        return false;

    if (childrenp) {
        *childrenp  = children;
        *nchildrenp = nchildren;
    } else
        XFree(children);

    return true;
}

// XRestackWindows(@subset) and verify that it did what we meant.
template<class T>
static bool referenceRestack(const T &subset,
                     Window **childrenp=NULL, unsigned *nchildrenp=NULL)
{
    int n = subset.count();
    WindowVec reversed(n);
    for (int i = 0; i < n; i++)
        reversed[n-1-i] = subset[i];

    Q_ASSERT(Dpy != NULL);
    if (!XRestackWindows(Dpy,
                        (Window*)reversed.constData(), reversed.count()))
        qFatal("XRestackWindows() failed");
    return verifyTree(subset, childrenp, nchildrenp);
}

// Like above, but use MRestacker.
static bool testRestack(const MRestacker::WindowStack &subset,
                     Window **childrenp=NULL, unsigned *nchildrenp=NULL)
{
    if (!testRestacker.restack(subset))
        qFatal("Mrs. Tacker failed");
    if (Dpy)
        return verifyTree(subset, childrenp, nchildrenp);

    MRestacker::WindowStack stack = testRestacker.getState();
    return verifyOrder(subset, stack, stack.count());
}

// Test that the supserstacker
// -- can produce the given @stack:ing
// -- and that the resulting order of the children is the same
//    as what XRestackWindows() produces from the same @fromStack.
//    This requires that
//    -- @fromStack and @fromState are given
//    -- and that they correspond to current, initial stacking.
static void dotest(const MRestacker::WindowStack &stack,
                   const WindowVec *fromStack=NULL,
                   const MRestacker *fromState=NULL)
{
    Window *children1, *children2;
    unsigned nchildren1, nchildren2;

    // Headless mode?
    if (!Dpy) {
        if (fromState)
            testRestacker.setState(*fromState);
        QVERIFY(testRestack(stack));
        return;
    }

    // Standalone or comparative test?
    if (!fromStack) {
        QVERIFY(testRestack(stack));

        // Eat a random number of events as a window manager would do.
        unsigned nevents = XPending(Dpy);
        if (nevents > 0) {
            for (nevents = random() % nevents; nevents > 0; nevents--)
            while (XPending(Dpy)) {
                XEvent xev;
                XNextEvent(Dpy, &xev);
                testRestacker.event(&xev);
            }
        }

        return;
    }

    // @children1 <- the outcome of superstacking
    testRestacker.setState(*fromState);
    QVERIFY(referenceRestack(*fromStack));
    QVERIFY(testRestack(stack, &children1, &nchildren1));

    // If all @managedWindows have been @stack:ed it's not necessary
    // to validate the outcome with XRestackWindows() because verifyTree()
    // has already done that.
    if (stack.count() < fromStack->count()) {
        // @children2 <- the outcome of XRestackWindows()
        QVERIFY(referenceRestack(*fromStack));
        QVERIFY(referenceRestack(stack, &children2, &nchildren2));
    
        // verify that the outcomes are the same
        QCOMPARE(nchildren1, nchildren2);
        for (unsigned i = 0; i < nchildren1; i++)
            QCOMPARE(children1[i], children2[i]);
        XFree(children2);
    }
    XFree(children1);
}

// Open @Dpy, create @fakeRoot and @optNWindows @managedWindows.
void SuperStackerTest::initTestCase()
{
    if (!optHeadless) {
        Dpy = XOpenDisplay(NULL);
        if (!Dpy)
            qWarning("couldn't connect to X, degrading to headless mode");
    }

    // Create @managedWindows.
    if (Dpy != NULL) {
        fakeRoot = XCreateSimpleWindow(Dpy, DefaultRootWindow(Dpy),
                                       0, 0, 100, 100, 0, 0, 0);
        QVERIFY(fakeRoot != None);

        for (int i = 0; i < optNWindows; i++)
            managedWindows << XCreateSimpleWindow(Dpy, fakeRoot,
                                                  0, 0, 100, 100, 0, 0, 0);
        QVERIFY(!managedWindows.contains(None));

        // Randomize the reference order to make sure that the superstacker
        // doesn't depend on the XIDs.
        std::random_shuffle(managedWindows.begin(), managedWindows.end(),
                            libcRandom);
        referenceRestack(managedWindows);
    } else {
        // Make up a bunch of unique XID:s.
        qDebug("initializing headless mode");
        while (managedWindows.count() < optNWindows) {
            Window win = 1 + random();
            if (!managedWindows.contains(win))
                managedWindows << win;
        }
    }

    // Initialize MRestacker and tell it what windows are managed.
    testRestacker.init(Dpy, fakeRoot);
    testRestacker.setState(managedWindows);
    QVERIFY(testRestacker.verifyState(managedWindows));
    if (Dpy)
        QVERIFY(testRestacker.verifyState());
    QVERIFY(testRestacker.getState() == managedWindows.toList());
}

void SuperStackerTest::cleanupTestCase()
{
    if (Dpy)
        XCloseDisplay(Dpy);
    Dpy = NULL;
}

// Reset planner statistics before each test function.
void SuperStackerTest::init()
{
    testRestacker.resetStats();
    verifyStats(NULL, 0);
}

// Execute @testFun so that all restacking is done from the initial order
// of @managedWindows.  Unless we're operating headless, also compare the
// result with XRestackWindows().
template<typename TFun>
static void comparativeTest(TFun testFun)
{
    if (Dpy) {
        QVERIFY(XSelectInput(Dpy, fakeRoot, None));
        referenceRestack(managedWindows);
        XSync(Dpy, True); // discard events
    }

    MRestacker refState;
    refState.setState(managedWindows);
    testFun(&managedWindows, &refState);
}

// Just test whether %MRestacker can produce whatever stacking @testFun
// throws at it.  This requires and tests the state tracking.
template<typename TFun>
static void standaloneTest(TFun testFun)
{
    if (Dpy) {
        referenceRestack(managedWindows);
        QVERIFY(XSelectInput(Dpy, fakeRoot, SubstructureNotifyMask));
        XSync(Dpy, True);
    }

    testRestacker.setState(managedWindows);
    testFun(NULL, NULL);
}

// Up to @optExhaustiveCut number of elements, take all possible stackings
// of @managedWindows and check whether the superstacker can produce them.
static void exhaustiveTest(const WindowVec *fromStack,
                           const MRestacker *fromState)
{
    unsigned ncombs = 0;

    // Generate all 1, 2, ..., @optExhaustiveCut-element repeating
    // combinations of @managedWindows.  We need the cut to prevent
    // combinatoric explosion.
    QVector<unsigned> origOrder;
    while (origOrder.count() < optExhaustiveCut)
        origOrder.append(origOrder.count());

    QVector<unsigned> setOrder = origOrder;
    do { // one permutation => some combinations
        MRestacker::WindowStack stack;
        foreach (unsigned idx, setOrder)
            stack.append(managedWindows[idx]);
        dotest(stack, fromStack, fromState); ncombs++;

        for (unsigned i = 0; stack.count() > 1; i++) {
            bool final = !(setOrder[i] < setOrder[i+1]);
            stack.removeFirst();
            dotest(stack, fromStack, fromState); ncombs++;
            if (final)
                break;
        }
        std::next_permutation(setOrder.begin(), setOrder.end());
    } while (setOrder != origOrder);

    // Verify that we've tried all the combinations we were meant to.
    qDebug("tested %u combinations", ncombs);
    QCOMPARE(ncombs, numberOfSubsets(origOrder.count()));

    // Verify that performance is as expected.
    verifyStats(fromStack
                ? &testExhaustiveComparativeStats
                : &testExhaustiveStandaloneStats, origOrder.count());
    printStats();
}

void SuperStackerTest::testExhaustiveComparative()
{
    comparativeTest(exhaustiveTest);
}

void SuperStackerTest::testExhaustiveStandalone()
{
    standaloneTest(exhaustiveTest);
}

// Test all rotations of the given @stack:ing.  When finished,
// @stack will be the same as when started.
static unsigned rottest(MRestacker::WindowStack &stack,
                        const WindowVec *fromStack,
                        const MRestacker *fromState)
{
    for (unsigned i = stack.count(); i > 0; i--) {
        dotest(stack, fromStack, fromState);
        stack.prepend(stack.takeLast());
    }
    return stack.count();
}

// Test rotating restackings, ie. when some windows are lowered
// to the bottom from the top or vica versa.
static void rotationTests(const WindowVec *fromStack,
                           const MRestacker *fromState)
{
    unsigned nrots = 0;
    MRestacker::WindowStack stack;
//    const LearntStatsTbl *stats = fromStack
//        ? &testRotationsComparativeStats : &testRotationsStandaloneStats;

    // Test all possible rotations of @managedWindows[0], [0:1], [0:2], ...
    while (stack.count() < managedWindows.count()) {
        stack.append(managedWindows[stack.count()]);
        nrots += rottest(stack, fromStack, fromState);

        // Just for fun test the reverse too.
        MRestacker::WindowStack reverse;
        while (reverse.count() < stack.count())
            reverse.prepend(stack[reverse.count()]);
        nrots += rottest(reverse, fromStack, fromState);

//  FIXME: stats don't match because aggressive is chosen more often due
//  to naiveOpsFinder(). It makes little sense to compare exact stats anyway,
//  it'd be better to check only the number of configures generated...
//        verifyStats(stats, stack.count()-1);
    }
    qDebug("tested %u rotations", nrots);
    printStats();
}

void SuperStackerTest::testRotationsComparative()
{
    comparativeTest(rotationTests);
}

void SuperStackerTest::testRotationsStandalone()
{
    standaloneTest(rotationTests);
}

// Test the @perm:utation and the inverse thereof of @managedWindows.
static unsigned mantest(QVector<unsigned> const &perm,
                        const WindowVec *fromStack,
                        const MRestacker *fromState)
{
    MRestacker::WindowStack stack;

    foreach (unsigned i, perm)
        stack.append(managedWindows[i]);
    dotest(stack, fromStack, fromState);

    for (int i = 0; i < perm.count(); i++)
        stack[perm[i]] = managedWindows[i];
    dotest(stack, fromStack, fromState);

    return 2;
}

// Test some hairy stackings.
static void presetTests(const WindowVec *fromStack,
                        const MRestacker *fromState)
{
    unsigned nmanual = 0;
    if (managedWindows.count() < 10)
        QSKIP("at least 10 windows needed", SkipSingle);

    // (0)(1, 4, 8)(2, 7)(3, 5, 6)(9)
    nmanual += mantest(QVector<unsigned>()
            << 0 << 8 << 7 << 6 << 1 << 3 << 5 << 2 << 4 << 9,
            fromStack, fromState);
    // (0, 4, 5)(9, 2, 1)(3)(8, 7, 6)
    nmanual += mantest(QVector<unsigned>()
            << 5 << 2 << 9 << 3 << 0 << 4 << 7 << 8 << 6 << 1,
            fromStack, fromState);
    // (0, 9, 2, 1, 4, 5)(3)(8, 7, 6)
    nmanual += mantest(QVector<unsigned>()
            << 5 << 2 << 9 << 3 << 1 << 4 << 7 << 8 << 6 << 0,
            fromStack, fromState);

    qDebug("tested %u g*i permutations", nmanual);
    verifyStats(fromStack
                ? &testPresetComparativeStats
                : &testPresetStandaloneStats, 0);
    printStats();
}

void SuperStackerTest::testPresetComparative()
{
    comparativeTest(presetTests);
}

void SuperStackerTest::testPresetStandalone()
{
    standaloneTest(presetTests);
}

// Test random stackings.
static void randomTest(const WindowVec *fromStack,
                       const MRestacker *fromState)
{
    if (optLearning)
        QSKIP("nothing to learn", SkipSingle);

    // Generate @nrandom random permutations of @managedWindows.
    MRestacker::WindowStack stack(managedWindows.toList());
    for (unsigned i = 0; i < optNRandomTests; i++) {
        std::random_shuffle(stack.begin(), stack.end(), libcRandom);
        dotest(stack, fromStack, fromState);
    }

    qDebug("tested %u random permutations", optNRandomTests);
    printStats();
}

void SuperStackerTest::testRandomComparative()
{
    comparativeTest(randomTest);
}

void SuperStackerTest::testRandomStandalone()
{
    standaloneTest(randomTest);
}

// Convert the argument list to a %WindowVec.
static WindowVec &mkWindowVec(WindowVec &wins, ...)
{
    Window win;
    va_list args;

    wins.resize(0);
    va_start(args, wins);
    while ((win = va_arg(args, Window)) != None)
        wins << win;
    va_end(args);
    return wins;
}

// Test that windowCreated(), windowConfigured() and windowDestroyed() changes
// the internal state as expected.
void SuperStackerTest::testState()
{
    MRestacker mrs;
    WindowVec wins;

    if (optLearning)
        QSKIP("nothing to learn", SkipSingle);

    // Build up a window stack.
    mrs.windowCreated(3);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, 3, None)));
    mrs.windowCreated(4);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, 3, 4, None)));
    mrs.windowCreated(1, 3);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, 1, 3, 4, None)));
    mrs.windowCreated(2, 3);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, 1, 2, 3, 4, None)));
    mrs.windowCreated(5);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, 1, 2, 3, 4, 5, None)));

    // Move the windows around.
    mrs.windowConfigured(1, 2);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, 2, 1, 3, 4, 5, None)));
    mrs.windowConfigured(1, 4);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, 2, 3, 4, 1, 5, None)));
    mrs.windowConfigured(1, 5);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, 2, 3, 4, 5, 1, None)));
    mrs.windowConfigured(1, None);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, 1, 2, 3, 4, 5, None)));
    mrs.windowConfigured(2, None);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, 2, 1, 3, 4, 5, None)));
    mrs.windowConfigured(5, 3);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, 2, 1, 3, 5, 4, None)));
    mrs.windowConfigured(2, 4);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, 1, 3, 5, 4, 2, None)));

    // Tear down the stack.
    mrs.windowDestroyed(5);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, 1, 3, 4, 2, None)));
    mrs.windowDestroyed(1);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, 3, 4, 2, None)));
    mrs.windowDestroyed(2);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, 3, 4, None)));
    mrs.windowConfigured(4, None);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, 4, 3, None)));
    mrs.windowConfigured(4, 3);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, 3, 4, None)));
    mrs.windowDestroyed(3);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, 4, None)));
    mrs.windowConfigured(4, None);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, 4, None)));
    mrs.windowDestroyed(4);
    QVERIFY(mrs.verifyState(mkWindowVec(wins, None)));
}

// Test insertWindow() and windowDestroyed() by random stacking operations.
void SuperStackerTest::testStateRandom()
{
    MRestacker mrs;
    WindowVec wins;
    bool bottomFirst;

    if (optLearning)
        QSKIP("nothing to learn", SkipSingle);

    // Verify that MRestacker is born with an empty state,
    // and that setting an empty set of windows works.
    mrs.verifyState(wins);
    mrs.setState(wins, bottomFirst=false);
    QVERIFY(mrs.verifyState(wins, bottomFirst));
    mrs.setState(wins, bottomFirst=true);
    QVERIFY(mrs.verifyState(wins, bottomFirst));

    // Test until each operations is executed >= @optNRandomTests times.
    unsigned nadds = 0, nmoves = 0, nremoves = 0, nshuffles = 0;
    while (nadds < optNRandomTests
           || nmoves < optNRandomTests
           || nremoves < optNRandomTests
           || nshuffles < optNRandomTests) {
        enum test { ADD, MOVE, REMOVE, SHUFFLE, LAST } what;
        switch (what = test(random() % int(LAST))) {
        default:
            break;
        case REMOVE:
            // Exercise windowDestroyed() by removing a random window,
            // if there's any.  Otherwise ADD one.
            if (!wins.isEmpty()) {
                unsigned idx = random() % wins.count();
                mrs.windowDestroyed(wins[idx]);
                wins.remove(idx);
                QVERIFY(mrs.verifyState(wins, bottomFirst));
                nremoves++;
                break;
            }
        case MOVE:
            // Exercise configureWindow() by moving a window around,
            // or ADD:ing one if there's nothing to move.
            if (!wins.isEmpty()) {
                int idx;
                Window win, below;

                // What to move and where.
                idx = random() % wins.count();
                win = wins[idx];
                wins.remove(idx);
                idx = random() % (wins.count()+1);

                // What shall be @below?
                if (bottomFirst)
                    // BOTTOM, win1, win2, ..., winN, TOP
                    below = idx > 0 ? wins[idx-1] : None;
                else
                    // TOP, win1, win2, ..., winN, BOTTOM
                    below = idx < wins.count() ? wins[idx] : None;

                wins.insert(idx, win);
                mrs.windowConfigured(win, below);
                QVERIFY(mrs.verifyState(wins, bottomFirst));
                nmoves++;
                break;
            }
        case ADD: {
            // Exercise createWindow() by adding a new window
            // at a random place.
            int idx;
            Window win, above;

            // What to insert and where.
            do win = 1+random(); while (wins.contains(win));
            idx = random() % (wins.count()+1);

            // What shall be @above?
            if (bottomFirst)
                // BOTTOM, win1, win2, ..., winN, TOP
                above = idx < wins.count() ? wins[idx] : None;
            else
                // TOP, win1, win2, ..., winN, BOTTOM
                above = idx > 0 ? wins[idx-1] : None;

            wins.insert(idx, win);
            mrs.windowCreated(win, above);
            QVERIFY(mrs.verifyState(wins, bottomFirst));
            nadds++;
            break;
        }
        case SHUFFLE:
            // Exercise setState() by setting a random order.
            // Also change @bottomFirst randomly.
            std::random_shuffle(wins.begin(), wins.end(), libcRandom);
            mrs.setState(wins, bottomFirst = random() % 2);
            QVERIFY(mrs.verifyState(wins, bottomFirst));
            nshuffles++;
            break;
        }
    }
    qDebug("\n         "
           "tested nadds=%u, nmoves=%u, nremoves=%u, nshuffles=%u",
            nadds, nmoves, nremoves, nshuffles);
}

int main(int argc, char *argv[])
{
    MCompositeManager app(argc, argv);
    // Parse -nox, -learn, -seed, -nrandom, -nwindows, and -maxwindows.
    int seed = -1;
    char *prgname = argv[0];
    for (; argv[1]; argc--, argv++) {
        if (!strcmp(argv[1], "-nox")) {
            optHeadless = true;
            continue;
        } if (!strcmp(argv[1], "-learn")) {
            optLearning = true;
            continue;
        }

        if (!argv[2])
            break;
        if (!strcmp(argv[1], "-seed"))
            seed = atoi(argv[2]);
        else if (!strcmp(argv[1], "-nrandom"))
            optNRandomTests = atoi(argv[2]);
        else if (!strcmp(argv[1], "-nwindows")) {
            optNWindows = atoi(argv[2]);
            if (optExhaustiveCut > optNWindows)
                optExhaustiveCut = optNWindows;
        } else if (!strcmp(argv[1], "-maxwindows")) {
            optExhaustiveCut = atoi(argv[2]);
            if (optNWindows < optExhaustiveCut)
                optNWindows = optExhaustiveCut;
        } else
            break;
        argc--; argv++;
    }
    argv[0] = prgname;

    // Take @optExhaustiveCut as an indicator of how much the user
    // is willing to wait for us.
    Q_ASSERT(optExhaustiveCut <= optNWindows);
    if (optLearning)
        optExhaustiveCut = optNWindows;
    if (!optNRandomTests)
        optNRandomTests = numberOfSubsets(optExhaustiveCut);

    // Make the test reproducible.
    if (seed < 0) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        seed = tv.tv_usec;
    }
    srandom(seed);
    qDebug("random number generator seeded with %d", seed);
    qDebug("number of managed windows: %d, exhaustive cut: %d",
            optNWindows, optExhaustiveCut);

    // Go
    SuperStackerTest test;
    return QTest::qExec(&test, argc, argv);
}
