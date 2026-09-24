#include <QTest>

#include "ui/Snap.h"

using namespace qiyaa::snap;

class TestSnap : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void snapsToRightEdgeOfOther() {
        // A window dragged to 5px right of another sticks to it.
        const QRect other(0, 0, 275, 116);
        const QRect moving(280, 3, 275, 116);
        QCOMPARE(snapToOthers(moving, {other}), QPoint(275, 0));
    }
    void snapsBelowOther() {
        const QRect other(100, 100, 275, 116);
        const QRect moving(104, 222, 275, 116);
        QCOMPARE(snapToOthers(moving, {other}), QPoint(100, 216));
    }
    void farAwayDoesNotSnap() {
        const QRect other(0, 0, 275, 116);
        const QRect moving(500, 500, 275, 116);
        QCOMPARE(snapToOthers(moving, {other}), QPoint(500, 500));
    }
    void snapsToScreenEdges() {
        const QRect screen(0, 0, 1920, 1080);
        QCOMPARE(snapWithin(QRect(10, 1000, 275, 116), screen), QPoint(0, 964));
    }
    void clampKeepsWindowOnScreen() {
        const QRect screen(0, 0, 1920, 1080);
        QCOMPARE(clampInside(QRect(1900, -50, 275, 116), screen), QPoint(1645, 0));
        QCOMPARE(clampInside(QRect(-500, 2000, 275, 116), screen), QPoint(0, 964));
    }
    void clampRespectsScreenOffset() {
        // Second monitor to the left, top panel of 32px.
        const QRect screen(-1280, 32, 1280, 992);
        QCOMPARE(clampInside(QRect(-1300, 0, 275, 116), screen), QPoint(-1280, 32));
    }
    void picksScreenWithLargestOverlap() {
        const QList<QRect> screens{QRect(0, 0, 1920, 1080), QRect(1920, 0, 2560, 1440)};
        QCOMPARE(pickScreen(QRect(1900, 10, 275, 116), screens), screens[1]);
        QCOMPARE(pickScreen(QRect(1700, 10, 275, 116), screens), screens[0]);
    }
    void lostWindowGoesToNearestScreen() {
        // Monitor that the window was on got unplugged.
        const QList<QRect> screens{QRect(0, 0, 1920, 1080)};
        const QRect lost(3000, 200, 275, 116);
        QCOMPARE(resolveDragPosition(lost, {}, screens), QPoint(1645, 200));
    }
};

QTEST_GUILESS_MAIN(TestSnap)
#include "test_snap.moc"
