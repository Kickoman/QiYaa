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

QStringList Ids(const QList<Core::Track>& tracks) {
    QStringList ids;
    for (const Core::Track& track : tracks) {
        ids << track.key.id;
    }
    return ids;
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
        queue.appendContinuation(Tracks(3, QStringLiteral("w")), std::make_shared<Tests::FakeContinuation>(), {});
        QCOMPARE(Ids(queue), (QStringList{"0", "1", "w0", "w1", "w2"}));
        QCOMPARE(PlayOrder(queue), (QList<int>{0, 1, 2, 3, 4}));
        QCOMPARE(queue.previous(), 3);
        queue.select(2);
        queue.setShuffle(true);
        queue.setRepeat(true);
        QCOMPARE(queue.next(), 3);
    }

    void shuffleOrdersTheHeadAndTheContinuationFollowsInItsOrder() {  // CONT-09
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(4)));
        queue.setShuffle(true);
        QVERIFY(queue.shuffleActive());
        queue.appendContinuation(Tracks(2, QStringLiteral("w")), std::make_shared<Tests::FakeContinuation>(), {});
        const QList<int> order = PlayOrder(queue);
        QCOMPARE(order.size(), 6);
        QCOMPARE(QSet<int>(order.cbegin(), order.cbegin() + 4), (QSet<int>{0, 1, 2, 3}));
        QCOMPARE(order.mid(4), (QList<int>{4, 5}));
        queue.setShuffleAlgorithm(Core::ShuffleAlgorithm::Random);
        queue.select(0);
        QCOMPARE(queue.remaining(), Core::kEndlessTracks);
    }

    void repeatWrapsTheHeadAndTheContinuationWaitsBehindIt() {  // CONT-10
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(2)));
        queue.appendContinuation(Tracks(1, QStringLiteral("w")), std::make_shared<Tests::FakeContinuation>(), {});
        queue.setRepeat(true);
        queue.select(1);
        QCOMPARE(queue.next(), 0);
        QCOMPARE(queue.remaining(), Core::kEndlessTracks);
        queue.setRepeat(false);
        QCOMPARE(queue.next(), 2);
        QCOMPARE(queue.remaining(), 2);
    }

    void aNewQueueForgetsTheHistoryAndTheStall() {  // CONT-01, CONT-05
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(3)));
        const auto wave = std::make_shared<Tests::FakeContinuation>();
        queue.setHistoryWave(wave);
        queue.appendContinuation(Tracks(1, QStringLiteral("w")), wave, {});
        for (int index : {0, 2, 0, 3}) {
            queue.select(index);
            queue.noteStarted(queue.entries()[index]);
        }
        QCOMPARE(Ids(queue.history()), (QStringList{"0", "2"}));
        QVERIFY(queue.historyWaveGave());
        queue.setStalled(true);
        queue.replace(HeadOf(Tracks(1)));
        QVERIFY(queue.history().isEmpty());
        QVERIFY(!queue.historyWave());
        QVERIFY(!queue.historyWaveGave());
        QVERIFY(!queue.isStalled());
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

    void newSeedsDropTheOutdatedVibeOnlyWhenNothingWaits() {  // HOST-11
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(1)));
        const auto wave = std::make_shared<Tests::FakeContinuation>();
        queue.setHistoryWave(wave);
        queue.setItems(Items({"a"}), Core::SeedHint{.seeds = {QStringLiteral("track:1")}, .version = 1});
        queue.appendContinuation(Tracks(2, QStringLiteral("w")), wave, 1);
        queue.setItems(Items({"a"}), Core::SeedHint{.seeds = {QStringLiteral("track:2")}, .version = 2});
        QCOMPARE(Ids(queue), (QStringList{"0", "a", "w0", "w1"}));
        QVERIFY(queue.isStale(1));
        QVERIFY(queue.isStale(std::nullopt));
        QVERIFY(!queue.isStale(2));
        queue.select(1);
        QCOMPARE(Ids(queue), (QStringList{"0", "a"}));
        queue.setItems({}, Core::SeedHint{.version = 3});
        QVERIFY(!queue.isStale(1));
        queue.endLayer();
        QVERIFY(queue.isStale(1));
        QVERIFY(!queue.isStale(std::nullopt));
    }

    void newSeedsEndTheStall() {  // HOST-11
        Core::Queue queue;
        queue.setItems({}, Core::SeedHint{.seeds = {QStringLiteral("track:1")}, .version = 1});
        queue.setStalled(true);
        queue.setItems({}, Core::SeedHint{.seeds = {QStringLiteral("track:1")}, .version = 1});
        QVERIFY(queue.isStalled());
        queue.setItems({}, Core::SeedHint{.version = 2});
        QVERIFY(queue.isStalled());
        queue.setItems({}, Core::SeedHint{.seeds = {QStringLiteral("track:2")}, .version = 3});
        QVERIFY(!queue.isStalled());
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

    void anotherBaseTakesTheItemsAnEarlierLayerLeft() {  // HOST-21, HOST-36, HOST-32
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(3)));
        queue.setItems(Items({"a", "b"}), Core::SeedHint{});
        queue.endLayer();
        queue.setItems(Items({"c"}), Core::SeedHint{});
        queue.replaceBase({});
        QCOMPARE(Ids(queue), (QStringList{"0", "c"}));
        QCOMPARE(PlayOrder(queue), (QList<int>{0, 1}));
    }

    void theLayerEndKeepsTheWaitingItemsFirstAndDropsTheJamWave() {  // HOST-32
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(2)));
        queue.setItems(Items({"a"}), Core::SeedHint{.version = 1});
        queue.appendContinuation(Tracks(2, QStringLiteral("w")), std::make_shared<Tests::FakeContinuation>(), 1);
        queue.endLayer();
        QCOMPARE(Ids(queue), (QStringList{"0", "a", "1"}));
        QVERIFY(queue.entries()[1].itemId.isEmpty());
        QCOMPARE(IdAt(queue, queue.next()), QStringLiteral("a"));
    }

    void removingTheCurrentTrackLeavesTheNextItemWaiting() {  // HOST-36
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(3)));
        queue.select(1);
        queue.setItems(Items({"a", "b"}), Core::SeedHint{});
        QVERIFY(queue.remove({1}));
        QCOMPARE(Ids(queue), (QStringList{"0", "a", "b", "2"}));
        QCOMPARE(queue.cursor(), -1);
        QVERIFY(queue.entries()[1].pending);
        queue.setItems(Items({"a", "b", "c"}), Core::SeedHint{});
        QCOMPARE(Ids(queue), (QStringList{"0", "a", "b", "c", "2"}));
        QCOMPARE(queue.firstToPlay(), 1);
        queue.select(queue.firstToPlay());
        QCOMPARE(PlayOrder(queue), (QList<int>{1, 2, 3, 4}));
    }

    void itemsAnEarlierLayerLeftWaitBehindTheNewOnes() {  // HOST-12, HOST-32
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(2)));
        queue.setItems(Items({"a", "b"}), Core::SeedHint{});
        queue.endLayer();
        queue.setItems({}, Core::SeedHint{});
        QCOMPARE(Ids(queue), (QStringList{"0", "a", "b", "1"}));
        queue.setItems(Items({"c"}), Core::SeedHint{});
        QCOMPARE(Ids(queue), (QStringList{"0", "c", "a", "b", "1"}));
        QCOMPARE(PlayOrder(queue), (QList<int>{0, 1, 2, 3, 4}));
    }

    void aStepBackFromTheVibeToTheHeadAndNextComesBack() {  // SHUF-02, CONT-09, CONT-11
        for (const bool shuffle : {false, true}) {
            Core::Queue queue;
            queue.replace(HeadOf(Tracks(6)));
            queue.setShuffle(shuffle);
            queue.appendContinuation(Tracks(3, QStringLiteral("w")), std::make_shared<Tests::FakeContinuation>(), {});
            int lastOfHead = queue.cursor();
            while (queue.entries()[queue.next()].part == Core::EntryPart::Head) {
                lastOfHead = queue.select(queue.next());
            }
            const int vibe = queue.select(queue.next());
            QCOMPARE(IdAt(queue, vibe), QStringLiteral("w0"));
            const int back = queue.stepBack();
            QCOMPARE(back, lastOfHead);
            queue.select(back);
            QCOMPARE(queue.next(), vibe);
            QCOMPARE(queue.remaining(), 4);
            queue.select(vibe + 1);
            queue.select(queue.stepBack());
            QCOMPARE(queue.next(), vibe + 1);
        }
    }

    void aStepBackFromTheVibeToAPlayedItemAndNextComesBack() {  // CONT-11, HOST-32
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(1)));
        queue.setItems(Items({"a"}), Core::SeedHint{});
        queue.endLayer();
        queue.appendContinuation(Tracks(2, QStringLiteral("w")), std::make_shared<Tests::FakeContinuation>(), {});
        queue.select(1);
        queue.select(2);
        queue.select(queue.stepBack());
        QCOMPARE(IdAt(queue, queue.cursor()), QStringLiteral("a"));
        QCOMPARE(IdAt(queue, queue.next()), QStringLiteral("w0"));
    }

    void playNextWaitsRightBehindTheCurrentTrackInFrontOfEarlierOnes() {  // ADD-01
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(4)));
        queue.select(1);
        queue.add(Tracks(1, QStringLiteral("a")), Core::AddPlacement::Next);
        QList<Core::Track> later = Tracks(3, QStringLiteral("b"));
        later[1].available = false;
        queue.add(later, Core::AddPlacement::Next);
        QCOMPARE(Ids(queue), (QStringList{"0", "1", "b0", "b2", "a0", "2", "3"}));
        QCOMPARE(queue.cursor(), 1);
        QCOMPARE(PlayOrder(queue), (QList<int>{1, 2, 3, 4, 5, 6}));
    }

    void waitingTracksPlayOnceOutsideTheShuffledPass() {  // ADD-02
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(4)));
        queue.setShuffle(true);
        queue.add(Tracks(1, QStringLiteral("a")), Core::AddPlacement::Next);
        QCOMPARE(IdAt(queue, queue.next()), QStringLiteral("a0"));
        const QList<int> order = PlayOrder(queue);
        QCOMPARE(order.size(), 5);
        QStringList played;
        for (int index : order) {
            played << IdAt(queue, index);
        }
        QCOMPARE(played.count(QStringLiteral("a0")), 1);
        queue.setRepeat(true);
        QVERIFY(IdAt(queue, queue.next()) != QStringLiteral("a0"));
        QVERIFY(!queue.entries()[queue.cursor()].pending);
    }

    void addingToTheEndJoinsTheHeadInFrontOfTheContinuation() {  // ADD-03
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(2)));
        queue.appendContinuation(Tracks(2, QStringLiteral("w")), std::make_shared<Tests::FakeContinuation>(), {});
        queue.add(Tracks(2, QStringLiteral("e")), Core::AddPlacement::End);
        QCOMPARE(Ids(queue), (QStringList{"0", "1", "e0", "e1", "w0", "w1"}));
        QCOMPARE(queue.entries()[2].part, Core::EntryPart::Head);
        QCOMPARE(PlayOrder(queue), (QList<int>{0, 1, 2, 3, 4, 5}));
        queue.select(3);
        queue.setRepeat(true);
        QCOMPARE(queue.next(), 0);
    }

    void addingToTheEndKeepsTheWaitingTracksRightBehindTheCurrentOne() {  // ADD-03
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(2)));
        queue.select(1);
        queue.add(Tracks(1, QStringLiteral("a")), Core::AddPlacement::Next);
        queue.add(Tracks(1, QStringLiteral("e")), Core::AddPlacement::End);
        QCOMPARE(Ids(queue), (QStringList{"0", "1", "a0", "e0"}));
        queue.select(2);
        queue.add(Tracks(1, QStringLiteral("f")), Core::AddPlacement::End);
        QCOMPARE(Ids(queue), (QStringList{"0", "1", "a0", "e0", "f0"}));
        QCOMPARE(PlayOrder(queue), (QList<int>{2, 3, 4}));
    }

    void addingToTheEndBeforeAPlayedWaitingTrackKeepsTheCursorOnIt() {  // ADD-03
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(2)));
        queue.select(1);
        queue.add(Tracks(1, QStringLiteral("a")), Core::AddPlacement::Next);
        queue.select(2);
        queue.add(Tracks(1, QStringLiteral("e")), Core::AddPlacement::End);
        QCOMPARE(Ids(queue), (QStringList{"0", "1", "e0", "a0"}));
        QCOMPARE(IdAt(queue, queue.cursor()), QStringLiteral("a0"));
        QCOMPARE(IdAt(queue, queue.next()), QStringLiteral("e0"));
    }

    void addingToTheEndOfAShuffledPassPlaysEachTrackOnce() {  // ADD-03, SHUF-01
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(5)));
        queue.setShuffle(true);
        queue.select(queue.next());
        queue.add(Tracks(3, QStringLiteral("e")), Core::AddPlacement::End);
        const QList<int> order = PlayOrder(queue);
        QCOMPARE(order.size(), 7);
        QCOMPARE(QSet<int>(order.cbegin(), order.cend()).size(), 7);
    }

    void addingToTheEndOfARunOutHeadWaitsBehindTheWaitingTracks() {  // ADD-04
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(1)));
        queue.appendContinuation(Tracks(2, QStringLiteral("w")), std::make_shared<Tests::FakeContinuation>(), {});
        queue.select(1);
        queue.add(Tracks(1, QStringLiteral("e")), Core::AddPlacement::End);
        queue.add(Tracks(1, QStringLiteral("a")), Core::AddPlacement::Next);
        queue.add(Tracks(1, QStringLiteral("f")), Core::AddPlacement::End);
        QCOMPARE(Ids(queue), (QStringList{"0", "w0", "a0", "e0", "f0", "w1"}));
        QCOMPARE(PlayOrder(queue), (QList<int>{1, 2, 3, 4, 5}));
    }

    void aWaveAddsToTheEndAsWaitingTracks() {  // ADD-04
        Core::Queue queue;
        queue.replace(Core::QueueSource{
            .continuation = std::make_shared<Tests::FakeContinuation>(), .given = Tracks(2, QStringLiteral("w"))
        });
        queue.add(Tracks(1, QStringLiteral("e")), Core::AddPlacement::End);
        QCOMPARE(Ids(queue), (QStringList{"w0", "e0", "w1"}));
        QCOMPARE(queue.entries()[1].part, Core::EntryPart::Item);
    }

    void removedWaitingTracksDoNotPlay() {  // ADD-07
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(3)));
        queue.add(Tracks(2, QStringLiteral("a")), Core::AddPlacement::Next);
        queue.add(Tracks(1, QStringLiteral("e")), Core::AddPlacement::End);
        QVERIFY(!queue.remove({1, 5}));
        QCOMPARE(Ids(queue), (QStringList{"0", "a1", "1", "2"}));
        QCOMPARE(PlayOrder(queue), (QList<int>{0, 1, 2, 3}));
    }

    void aNewQueueBeginsWithTheChosenTrack() {  // SRC-16, SRC-18, SHUF-01
        QList<Core::Track> tracks = Tracks(5);
        tracks[1].available = false;
        Core::Queue queue;
        queue.replace(Core::QueueSource{.head = tracks, .start = 3});
        QCOMPARE(IdAt(queue, queue.cursor()), QStringLiteral("3"));
        QCOMPARE(PlayOrder(queue), (QList<int>{2, 3}));
        queue.setShuffle(true);
        queue.replace(Core::QueueSource{.head = tracks, .start = 4});
        const QList<int> order = PlayOrder(queue);
        QCOMPARE(IdAt(queue, order.first()), QStringLiteral("4"));
        QCOMPARE(QSet<int>(order.cbegin(), order.cend()).size(), 4);
    }

    void anotherBaseBeginsWithTheChosenTrack() {  // HOST-38
        Core::Queue queue;
        queue.replace(HeadOf(Tracks(3)));
        queue.select(1);
        queue.setItems(Items({"a"}), Core::SeedHint{});
        queue.replaceBase(Core::QueueSource{.head = Tracks(4, QStringLiteral("x")), .start = 2});
        QCOMPARE(Ids(queue), (QStringList{"1", "a", "x0", "x1", "x2", "x3"}));
        QCOMPARE(PlayOrder(queue), (QList<int>{0, 1, 4, 5}));
        queue.setShuffle(true);
        queue.replaceBase(Core::QueueSource{.head = Tracks(4, QStringLiteral("y")), .start = 1});
        queue.setShuffle(true);
        QCOMPARE(IdAt(queue, queue.next()), QStringLiteral("y1"));
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
