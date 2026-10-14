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

QList<Core::Track> Tracks(int count, const QString& prefix = {}) {
    QList<Core::Track> tracks;
    for (int i = 0; i < count; ++i) {
        Core::Track track;
        track.key = {QStringLiteral("test"), prefix + QString::number(i)};
        tracks << track;
    }
    return tracks;
}

QList<Core::LayerItem> Items(const QStringList& ids) {
    QList<Core::LayerItem> items;
    for (const QString& id : ids) {
        Core::Track track;
        track.key = {QStringLiteral("test"), id};
        items << Core::LayerItem{.itemId = id, .track = track};
    }
    return items;
}

QStringList Ids(const Core::Queue& queue) {
    QStringList ids;
    for (const Core::QueueEntry& entry : queue.entries()) {
        ids << entry.track.key.id;
    }
    return ids;
}

QString IdAt(const Core::Queue& queue, int index) {
    return index >= 0 && index < queue.entries().size() ? queue.entries()[index].track.key.id : QString();
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
        queue.replace(HeadOf(Tracks(5)));
        queue.setShuffle(true);
        queue.select(2);
        QVERIFY(!queue.remove({4}));
        QCOMPARE(PlayOrder(queue).size(), 4);
    }

    void editsKeepTheCursorOnItsTrack() {
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(5)));
        queue.select(3);
        QVERIFY(!queue.remove({0}));
        QCOMPARE(queue.cursor(), 2);
        QCOMPARE(IdAt(queue, queue.next()), QStringLiteral("4"));
        QVERIFY(queue.remove({2}));
        QCOMPARE(queue.cursor(), 2);
        QCOMPARE(IdAt(queue, queue.cursor()), QStringLiteral("4"));
        QVERIFY(queue.remove({2}));
        QCOMPARE(queue.cursor(), 1);
        QVERIFY(!queue.remove({7, -1}));
        queue.remove({0, 1});
        QCOMPARE(queue.cursor(), -1);
    }

    void theWaitingItemsPlayFirstAndTheHeadGoesOnAfterThem() {  // HOST-12
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(4)));
        queue.select(1);
        queue.setItems(Items({"a", "b"}), Core::SeedHint{});
        QCOMPARE(Ids(queue), (QStringList{"0", "1", "a", "b", "2", "3"}));
        QCOMPARE(PlayOrder(queue), (QList<int>{1, 2, 3, 4, 5}));
        QVERIFY(!queue.entries()[2].pending);
    }

    void aJumpTakesTheWaitingItemsBehindTheNewCurrentTrack() {  // HOST-12
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(5)));
        queue.setItems(Items({"a", "b"}), Core::SeedHint{});
        const int jumped = queue.select(5);
        QCOMPARE(Ids(queue), (QStringList{"0", "1", "2", "3", "a", "b", "4"}));
        QCOMPARE(jumped, 3);
        QCOMPARE(queue.cursor(), 3);
        QCOMPARE(IdAt(queue, queue.next()), QStringLiteral("a"));
        QCOMPARE(queue.select(5), 4);
        QCOMPARE(Ids(queue), (QStringList{"0", "1", "2", "3", "b", "a", "4"}));
        QCOMPARE(IdAt(queue, queue.next()), QStringLiteral("a"));
    }

    void itemsAndLoadsLeaveTheShuffledPassAlone() {
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(8)));
        queue.setShuffle(true);
        const QString before = IdAt(queue, queue.next());
        queue.setItems(Items({"a"}), Core::SeedHint{});
        queue.select(queue.next());
        queue.appendContinuation(Tracks(2, QStringLiteral("w")), std::make_shared<Tests::FakeContinuation>(), {});
        QCOMPARE(IdAt(queue, queue.next()), before);
    }

    void theHeadThenTheContinuation() {  // CONT-11
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(2)));
        queue.appendContinuation(Tracks(2, QStringLiteral("w")), std::make_shared<Tests::FakeContinuation>(), {});
        QCOMPARE(Ids(queue), (QStringList{"0", "1", "w0", "w1"}));
        QCOMPARE(PlayOrder(queue), (QList<int>{0, 1, 2, 3}));
        QCOMPARE(queue.previous(), 2);
    }

    void remainingCountsWhatIsLeftInPlayOrder() {  // WAVE-05
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(3)));
        QCOMPARE(queue.remaining(), 3);
        queue.setItems(Items({"a", "b"}), Core::SeedHint{});
        QCOMPARE(queue.remaining(), 5);
        queue.setRepeat(true);
        QCOMPARE(queue.remaining(), Core::kEndlessTracks);
        queue.setRepeat(false);
        queue.setShuffleAlgorithm(Core::ShuffleAlgorithm::Random);
        queue.setShuffle(true);
        QCOMPARE(queue.remaining(), Core::kEndlessTracks);

        const auto vibe = std::make_shared<Tests::FakeContinuation>();
        queue.replace(Core::QueueSource{.continuation = vibe, .given = Tracks(5)});
        queue.select(2);
        QCOMPARE(queue.remaining(), 3);
        queue.select(0);
        QCOMPARE(queue.remaining(), 5);
    }

    void newSeedsDropTheStaleWaveOnlyWhenNothingWaits() {  // HOST-11
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(1)));
        const auto wave = std::make_shared<Tests::FakeContinuation>();
        queue.setItems(Items({"a"}), Core::SeedHint{.version = 1});
        queue.appendContinuation(Tracks(2, QStringLiteral("w")), wave, 1);
        queue.setItems(Items({"a"}), Core::SeedHint{.version = 2});
        QCOMPARE(Ids(queue), (QStringList{"0", "a", "w0", "w1"}));
        QVERIFY(queue.isStale(1));
        QVERIFY(!queue.isStale(2));
        queue.select(1);
        QCOMPARE(Ids(queue), (QStringList{"0", "a"}));
    }

    void anotherBaseKeepsTheCurrentTrackAndTheWaitingItems() {
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(4)));
        queue.select(2);
        queue.setItems(Items({"a"}), Core::SeedHint{});
        queue.replaceBase(HeadOf(Tracks(2, QStringLiteral("x")), QStringLiteral("X")));
        QCOMPARE(Ids(queue), (QStringList{"2", "a", "x0", "x1"}));
        QCOMPARE(queue.cursor(), 0);
        QCOMPARE(queue.entries()[0].part, Core::EntryPart::Other);
        QCOMPARE(queue.title(), QStringLiteral("X"));
        QCOMPARE(PlayOrder(queue), (QList<int>{0, 1, 2, 3}));
    }

    void theLayerEndKeepsTheWaitingItemsFirstAndDropsTheJamWave() {  // HOST-32
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(2)));
        queue.setItems(Items({"a"}), Core::SeedHint{.version = 1});
        queue.appendContinuation(Tracks(2, QStringLiteral("w")), std::make_shared<Tests::FakeContinuation>(), 1);
        const quint64 generation = queue.generation();
        queue.endLayer();
        QCOMPARE(Ids(queue), (QStringList{"0", "a", "1"}));
        QVERIFY(queue.entries()[1].itemId.isEmpty());
        QCOMPARE(IdAt(queue, queue.next()), QStringLiteral("a"));
        QVERIFY(queue.generation() != generation);
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
