#include "core/queue.h"
#include "core/queue_source.h"
#include "core/track.h"
#include "core/track_navigator.h"
#include "support/fake_continuation.h"

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTest>

#include <memory>

namespace {

QList<Core::Track> Tracks(int count) {
    QList<Core::Track> tracks;
    for (int i = 0; i < count; ++i) {
        Core::Track track;
        track.key = {QStringLiteral("test"), QString::number(i)};
        tracks << track;
    }
    return tracks;
}

Core::QueueSource HeadOf(const QList<Core::Track>& tracks, const QString& title = {}) {
    return Core::QueueSource{.title = title, .head = tracks};
}

QList<int> PlayOrder(Core::Queue& queue) {
    QList<int> order{queue.cursor()};
    for (int next = queue.next(); next >= 0 && order.size() <= queue.entries().size(); next = queue.next()) {
        order << next;
        queue.select(next);
    }
    return order;
}

}  // namespace

class TestQueue : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void onlyAvailableTracksAreQueued() {  // SRC-05
        QList<Core::Track> tracks = Tracks(3);
        tracks[1].available = false;
        Core::Queue queue;
        queue.replace(HeadOf(tracks, QStringLiteral("Q")));
        QCOMPARE(queue.entries().size(), 2);
        QCOMPARE(queue.cursor(), 0);
        QCOMPARE(queue.title(), QStringLiteral("Q"));
        queue.replace({});
        QCOMPARE(queue.cursor(), -1);
    }

    void aFiniteQueueGoesInOrderAndStopsAtItsEnd() {  // TR-03, TR-04
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(3)));
        QCOMPARE(PlayOrder(queue), (QList<int>{0, 1, 2}));
        QCOMPARE(queue.next(), -1);
    }

    void repeatWrapsToTheFirstTrack() {  // TR-05
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(3)));
        queue.setRepeat(true);
        queue.select(2);
        QCOMPARE(queue.next(), 0);
    }

    void previousGoesBackAndWrapsOnlyWithRepeat() {  // TR-01
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(3)));
        queue.select(2);
        QCOMPARE(queue.previous(), 1);
        queue.select(0);
        QCOMPARE(queue.previous(), 0);
        queue.setRepeat(true);
        QCOMPARE(queue.previous(), 2);
    }

    void anEndlessQueueNeitherWrapsNorShuffles() {  // WAVE-10, WAVE-12
        Core::Queue queue;
        queue.setShuffle(true);
        queue.setRepeat(true);
        queue.replace(Core::QueueSource{.continuation = std::make_shared<Tests::FakeContinuation>(), .given = Tracks(3)}
        );
        QVERIFY(queue.shuffle());
        QVERIFY(!queue.shuffleActive());
        QCOMPARE(PlayOrder(queue), (QList<int>{0, 1, 2}));
        QCOMPARE(queue.next(), -1);
    }

    void withoutRepeatsVisitsEachTrackOnceAndRepeatGoesRoundTheSameOrder() {  // SHUF-01
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(6)));
        queue.setShuffle(true);
        const QList<int> order = PlayOrder(queue);
        QCOMPARE(order.size(), 6);
        QCOMPARE(QSet<int>(order.cbegin(), order.cend()).size(), 6);
        QCOMPARE(queue.next(), -1);
        queue.setRepeat(true);
        QCOMPARE(queue.next(), order.first());
    }

    void previousFollowsTheStoredOrder() {  // SHUF-02
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(5)));
        queue.setShuffle(true);
        const QList<int> order = PlayOrder(queue);
        for (qsizetype i = order.size() - 1; i > 0; --i) {
            QCOMPARE(queue.previous(), order[i - 1]);
            queue.select(order[i - 1]);
        }
    }

    void editsRebuildTheShuffledPassFromTheCurrentTrack() {  // SHUF-04
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(4)));
        queue.setShuffle(true);
        queue.select(2);
        QVERIFY(queue.insert(4, Tracks(1)));
        QCOMPARE(PlayOrder(queue).size(), 5);
    }

    void editsKeepTheCursorOnItsTrack() {
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(4)));
        queue.select(2);
        QVERIFY(queue.insert(0, Tracks(1)));
        QCOMPARE(queue.cursor(), 3);
        QVERIFY(!queue.remove({0}));
        QCOMPARE(queue.cursor(), 2);
        QVERIFY(queue.remove({2}));
        QCOMPARE(queue.cursor(), 2);
        QVERIFY(queue.remove({2}));
        QCOMPARE(queue.cursor(), 1);
        QVERIFY(!queue.remove({7, -1}));
        queue.remove({0, 1});
        QCOMPARE(queue.cursor(), -1);
        QVERIFY(queue.insert(0, Tracks(2)));
        QCOMPARE(queue.cursor(), 0);
    }

    void aNewSourceStartsANewGeneration() {
        Core::Queue queue;
        const quint64 before = queue.generation();
        queue.setAsking(true);
        queue.replace(HeadOf(Tracks(1)));
        QVERIFY(queue.generation() != before);
        QVERIFY(!queue.isAsking());
    }
};

QTEST_GUILESS_MAIN(TestQueue)
#include "queue_test.moc"
