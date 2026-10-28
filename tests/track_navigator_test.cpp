#include "core/track_navigator.h"

#include <QList>
#include <QObject>
#include <QTest>

#include <optional>

class TestTrackNavigator : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void sequentialStopsOrWrapsAtTheBoundary() {
        auto navigator = Core::MakeTrackNavigator(std::nullopt);
        navigator->reset(4, 1);
        QCOMPARE(navigator->next(false), 2);
        QCOMPARE(navigator->previous(false), 0);
        navigator->select(3);
        QCOMPARE(navigator->next(false), -1);
        QCOMPARE(navigator->next(true), 0);
        navigator->select(0);
        QCOMPARE(navigator->previous(false), 0);
        QCOMPARE(navigator->previous(true), 3);
    }

    void randomLookaheadIsStableAndExcludesTheCurrentTrack() {
        auto navigator = Core::MakeTrackNavigator(Core::ShuffleAlgorithm::Random);
        navigator->reset(8, 3);
        for (int transition = 0; transition < 20; ++transition) {
            const int next = navigator->next(false);
            QVERIFY(next >= 0 && next < 8);
            QCOMPARE(navigator->next(false), next);
            QCOMPARE(navigator->next(true), next);
            navigator->select(next);
            QVERIFY(navigator->next(false) != next);
            QCOMPARE(navigator->previous(false), next > 0 ? next - 1 : 0);
        }
        navigator->reset(2, 0);
        QCOMPARE(navigator->next(false), 1);
        navigator->select(1);
        QCOMPARE(navigator->next(false), 0);
    }

    void shuffledLookaheadDoesNotConsumeEntries() {
        auto navigator = Core::MakeTrackNavigator(Core::ShuffleAlgorithm::WithoutRepeats);
        navigator->reset(8, 3);
        QList<int> order{3};
        for (int count = 1; count < 8; ++count) {
            const int next = navigator->next(false);
            QVERIFY(next >= 0 && next < 8);
            QVERIFY(!order.contains(next));
            QCOMPARE(navigator->next(false), next);
            order << next;
            navigator->select(next);
        }
        QCOMPARE(navigator->next(false), -1);
        QCOMPARE(navigator->next(true), 3);
        for (int position = 6; position >= 0; --position) {
            QCOMPARE(navigator->previous(false), order[position]);
            navigator->select(order[position]);
        }
        QCOMPARE(navigator->previous(false), 3);
        QCOMPARE(navigator->previous(true), order.back());
        navigator->select(order[4]);
        QCOMPARE(navigator->next(false), order[5]);
    }

    void emptyAndSingleTrackQueues_data() {
        QTest::addColumn<int>("algorithm");
        QTest::newRow("sequential") << -1;
        QTest::newRow("random") << static_cast<int>(Core::ShuffleAlgorithm::Random);
        QTest::newRow("shuffled") << static_cast<int>(Core::ShuffleAlgorithm::WithoutRepeats);
    }
    void emptyAndSingleTrackQueues() {
        QFETCH(int, algorithm);
        auto navigator = Core::MakeTrackNavigator(
            algorithm < 0 ? std::nullopt : std::optional(static_cast<Core::ShuffleAlgorithm>(algorithm))
        );
        navigator->reset(0, -1);
        QCOMPARE(navigator->next(false), -1);
        QCOMPARE(navigator->next(true), -1);
        QCOMPARE(navigator->previous(true), -1);
        navigator->reset(1, 0);
        QCOMPARE(navigator->next(false), -1);
        QCOMPARE(navigator->next(true), 0);
        QCOMPARE(navigator->previous(false), 0);
    }

    void eachNavigatorCanStartBeforeItsFirstTrack_data() { emptyAndSingleTrackQueues_data(); }
    void eachNavigatorCanStartBeforeItsFirstTrack() {
        QFETCH(int, algorithm);
        auto navigator = Core::MakeTrackNavigator(
            algorithm < 0 ? std::nullopt : std::optional(static_cast<Core::ShuffleAlgorithm>(algorithm))
        );
        navigator->reset(5, -1);
        const int first = navigator->next(false);
        QVERIFY(first >= 0 && first < 5);
        QCOMPARE(navigator->next(false), first);
        const bool random = algorithm == static_cast<int>(Core::ShuffleAlgorithm::Random);
        QCOMPARE(navigator->remaining(false), random ? Core::kEndlessTracks : 5);
        navigator->select(first);
        QCOMPARE(navigator->remaining(false), random ? Core::kEndlessTracks : 4);
        QCOMPARE(navigator->remaining(true), Core::kEndlessTracks);
        navigator->reset(1, -1);
        QCOMPARE(navigator->next(false), 0);
        QCOMPARE(navigator->remaining(false), 1);
    }

    void eachNavigatorCanBeginWithAChosenTrack_data() { emptyAndSingleTrackQueues_data(); }
    void eachNavigatorCanBeginWithAChosenTrack() {  // HOST-38
        QFETCH(int, algorithm);
        auto navigator = Core::MakeTrackNavigator(
            algorithm < 0 ? std::nullopt : std::optional(static_cast<Core::ShuffleAlgorithm>(algorithm))
        );
        for (int attempt = 0; attempt < 30; ++attempt) {
            navigator->resetBefore(6, 4);
            navigator->append(3);
            QCOMPARE(navigator->next(false), 4);
            navigator->resetBefore(1, 0);
            navigator->append(3);
            QCOMPARE(navigator->next(false), 0);
        }
        navigator->resetBefore(6, 4);
        QCOMPARE(navigator->next(false), 4);
        navigator->select(4);
        const int after = navigator->next(false);
        QVERIFY(after >= 0 && after < 6 && after != 4);
    }

    void tracksAppendedToASequentialPassPlayAfterItsLast() {  // ADD-03
        auto navigator = Core::MakeTrackNavigator(std::nullopt);
        navigator->reset(3, 2);
        QCOMPARE(navigator->next(false), -1);
        navigator->append(2);
        QCOMPARE(navigator->next(false), 3);
        navigator->select(4);
        QCOMPARE(navigator->next(true), 0);
    }

    void tracksAppendedToARandomTrackCanComeNext() {  // ADD-03, SHUF-08
        auto navigator = Core::MakeTrackNavigator(Core::ShuffleAlgorithm::Random);
        navigator->reset(1, 0);
        QCOMPARE(navigator->next(false), -1);
        navigator->append(1);
        QCOMPARE(navigator->next(false), 1);
        QCOMPARE(navigator->remaining(false), Core::kEndlessTracks);
    }

    void tracksAppendedToAShuffledPassComeAmongTheRestOfIt() {  // ADD-03, SHUF-01
        for (int attempt = 0; attempt < 50; ++attempt) {
            auto navigator = Core::MakeTrackNavigator(Core::ShuffleAlgorithm::WithoutRepeats);
            navigator->reset(6, 0);
            QList<int> played{0};
            for (int step = 0; step < 2; ++step) {
                played << navigator->next(false);
                navigator->select(played.back());
            }
            navigator->append(3);
            QCOMPARE(navigator->remaining(false), 6);
            QList<int> rest;
            for (int next = navigator->next(false); next >= 0; next = navigator->next(false)) {
                rest << next;
                navigator->select(next);
            }
            QCOMPARE(rest.size(), 6);
            for (int index = 0; index < 9; ++index) {
                QCOMPARE(played.count(index) + rest.count(index), 1);
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestTrackNavigator)
#include "track_navigator_test.moc"
