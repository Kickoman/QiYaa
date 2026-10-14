#include "audio/audio_engine.h"
#include "core/player.h"
#include "core/track.h"
#include "core/track_source.h"
#include "support/fake_continuation.h"
#include "support/fake_source.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>

#include <memory>

namespace {

const QString kFake = QStringLiteral("fake");

QList<Core::Track> Tracks(int count) {
    QList<Core::Track> tracks;
    for (int i = 1; i <= count; ++i) {
        Core::Track track;
        track.key = {kFake, QString::number(i)};
        track.title = QStringLiteral("Track %1").arg(i);
        track.durationMs = 3'000;
        tracks << track;
    }
    return tracks;
}

Core::QueueSource HeadOf(const QList<Core::Track>& tracks, const QString& title = QStringLiteral("Fake")) {
    return Core::QueueSource{.title = title, .head = tracks};
}

struct Stack {
    Audio::AudioEngine engine;
    Tests::FakeSource source;
    Core::SourceRegistry registry;
    Core::Player player{&engine, &registry};
    QStringList events;

    Stack() {
        registry.add(kFake, &source);
        engine.setVolume(0);
        QObject::connect(&player, &Core::Player::trackEvent, [this](Core::TrackEvent event, const Core::Track& track) {
            events << Tests::EventName(event) + u':' + track.key.id;
        });
    }
    bool start(int tracks) {
        if (!engine.init().ok) {
            return false;
        }
        player.setSource(HeadOf(Tracks(tracks)), false);
        return true;
    }
};

QList<Core::Player::State> States(const QSignalSpy& spy) {
    QList<Core::Player::State> states;
    for (const QList<QVariant>& arguments : spy) {
        states << arguments.at(0).value<Core::Player::State>();
    }
    return states;
}

}  // namespace

class TestPlayer : public QObject {
    Q_OBJECT
private:
    QByteArray mp3;

private Q_SLOTS:
    void initTestCase() {
        QFile file(QStringLiteral(QIYAA_TEST_DATA "/sine440_3s.mp3"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        mp3 = file.readAll();
    }

    void theLinkOfATrackLeftBehindIsDropped() {
        Stack stack;
        QVERIFY(stack.start(3));
        stack.player.playIndex(0);
        stack.player.playIndex(1);
        QCOMPARE(stack.source.asked(), (QStringList{"1", "2"}));
        const QPointer<Tests::FakeDownload> stale = stack.source.complete(0);
        QVERIFY(!stale);
        QVERIFY(stack.source.started().isEmpty());
        QVERIFY(stack.events.isEmpty());
        QVERIFY(stack.player.currentLink().isEmpty());
        QCOMPARE(stack.player.currentIndex(), 1);
        stack.source.complete(1);
        QCOMPARE(stack.source.started(), QStringList{"2"});
        QCOMPARE(stack.events, QStringList{"started:2"});
        QCOMPARE(stack.player.currentLink(), Tests::FakeSource::LinkOf("2"));
    }

    void aLinkAfterStopIsDropped() {
        Stack stack;
        QVERIFY(stack.start(2));
        stack.player.playIndex(0);
        stack.player.stop();
        QVERIFY(!stack.source.complete(0));
        QVERIFY(stack.source.started().isEmpty());
        QVERIFY(stack.events.isEmpty());
        QVERIFY(stack.player.currentLink().isEmpty());
        QCOMPARE(stack.engine.state(), Audio::AudioEngine::State::Stopped);
    }

    void aRetryAfterTheNetworkWaitIsDroppedAndNoneFollowsAStop() {  // ERR-01, ERR-02
        Stack stack;
        QVERIFY(stack.start(2));
        stack.player.setNetworkOnline(false);
        stack.player.playIndex(0);
        stack.source.fail(0, Core::FailureKind::Network);
        QCOMPARE(stack.player.state(), Core::Player::State::WaitingForNetwork);
        QCOMPARE(stack.source.pending(), 0);
        stack.player.setNetworkOnline(true);
        QCOMPARE(stack.source.asked(), (QStringList{"1", "1"}));
        stack.player.stop();
        QCOMPARE(stack.player.state(), Core::Player::State::Stopped);
        QVERIFY(!stack.source.complete(1));
        stack.player.setNetworkOnline(false);
        stack.player.setNetworkOnline(true);
        QCOMPARE(stack.source.asked().size(), 2);
        QCOMPARE(stack.source.pending(), 0);
        QVERIFY(stack.source.started().isEmpty());
        QVERIFY(stack.events.isEmpty());
    }

    void theLinkOfACancelledPreloadIsDropped() {  // QUAL-05
        Stack stack;
        QVERIFY(stack.start(3));
        stack.player.playIndex(0);
        const QPointer<Tests::FakeDownload> first = stack.source.complete(0);
        QVERIFY(first);
        first->feed(mp3);
        first->finish();
        QCOMPARE(stack.source.asked(), (QStringList{"1", "2"}));
        stack.source.changeFiles();
        QCOMPARE(stack.source.asked(), (QStringList{"1", "2", "2"}));
        QVERIFY(!stack.source.complete(1));
        QCOMPARE(stack.player.preloadedIndex(), -1);
        QVERIFY(stack.source.complete(2));
        QCOMPARE(stack.source.started(), (QStringList{"1", "2"}));
        QCOMPARE(stack.player.preloadedIndex(), 1);
        QCOMPARE(stack.player.nextLink(), Tests::FakeSource::LinkOf("2"));
    }

    void anotherSourcesFilesLeaveThePreloadAlone() {
        Stack stack;
        QVERIFY(stack.start(3));
        Tests::FakeSource other;
        stack.registry.add(QStringLiteral("other"), &other);
        stack.player.playIndex(0);
        const QPointer<Tests::FakeDownload> first = stack.source.complete(0);
        first->feed(mp3);
        first->finish();
        stack.source.complete(1);
        QCOMPARE(stack.player.preloadedIndex(), 1);
        other.changeFiles();
        QCOMPARE(stack.player.preloadedIndex(), 1);
        QCOMPARE(stack.source.asked(), (QStringList{"1", "2"}));
    }

    void aLoadForAReplacedQueueIsDropped() {
        Stack stack;
        QVERIFY(stack.start(0));
        const auto vibe = std::make_shared<Tests::FakeContinuation>();
        stack.player.setSource(
            Core::QueueSource{.title = QStringLiteral("Vibe"), .continuation = vibe, .given = Tracks(1)}, false
        );
        stack.player.playIndex(0);
        QCOMPARE(vibe->pending(), 1);
        stack.player.setSource(HeadOf(Tracks(2), QStringLiteral("Other")), false);
        vibe->answer(Tracks(3));
        QCOMPARE(stack.player.entries().size(), 2);
        const auto again = std::make_shared<Tests::FakeContinuation>();
        stack.player.changeSource(QStringLiteral("Vibe"), again, {});
        stack.player.requestMore();
        QCOMPARE(again->pending(), 1);
        stack.player.changeSource(QStringLiteral("Fixed"), nullptr, {});
        again->answer(Tracks(3));
        QCOMPARE(stack.player.entries().size(), 2);
    }

    void aTrackStartsUnderTheRulesOfItsStart() {  // HOST-15
        Stack stack;
        QVERIFY(stack.start(2));
        stack.player.playIndex(0);
        stack.player.changeSource(QStringLiteral("Jam"), nullptr, Core::QueueRules{.playReports = false});
        stack.source.complete(0);
        QCOMPARE(stack.events, QStringList{"started:1"});
        QVERIFY(stack.source.events().isEmpty());
        stack.player.stop();
        QCOMPARE(stack.events, (QStringList{"started:1", "skipped:1"}));
        QVERIFY(stack.source.events().isEmpty());
    }

    void eachPhaseShowsItsState() {
        Stack stack;
        QCOMPARE(stack.player.state(), Core::Player::State::Empty);
        QVERIFY(stack.start(2));
        QCOMPARE(stack.player.state(), Core::Player::State::Stopped);
        stack.player.playIndex(0);
        QCOMPARE(stack.player.state(), Core::Player::State::Loading);
        const QPointer<Tests::FakeDownload> first = stack.source.complete(0);
        QCOMPARE(stack.player.state(), Core::Player::State::Loading);
        first->feed(mp3);
        first->finish();
        QTRY_COMPARE(stack.player.state(), Core::Player::State::Playing);
        QCOMPARE(stack.player.currentBitrate(), 320);
        stack.player.pause();
        QTRY_COMPARE(stack.player.state(), Core::Player::State::Paused);
        stack.player.stop();
        QCOMPARE(stack.player.state(), Core::Player::State::Stopped);
        QVERIFY(stack.player.currentLink().isEmpty());
        QCOMPARE(stack.player.currentBitrate(), 0);
        const auto vibe = std::make_shared<Tests::FakeContinuation>();
        stack.player.setSource(
            Core::QueueSource{.title = QStringLiteral("Vibe"), .continuation = vibe, .given = Tracks(1)}, false
        );
        stack.player.playIndex(0);
        stack.source.complete(1);
        stack.player.next();
        QCOMPARE(stack.player.state(), Core::Player::State::WaitingForMore);
        stack.player.clearQueue();
        QCOMPARE(stack.player.state(), Core::Player::State::Empty);
    }

    void stopIsOneChangeOfState() {
        Stack stack;
        QVERIFY(stack.start(2));
        QSignalSpy states(&stack.player, &Core::Player::stateChanged);
        stack.player.playIndex(0);
        const QPointer<Tests::FakeDownload> first = stack.source.complete(0);
        first->feed(mp3);
        first->finish();
        QTRY_VERIFY(!states.isEmpty() && States(states).last() == Core::Player::State::Playing);
        states.clear();
        stack.player.stop();
        QTest::qWait(50);
        QCOMPARE(States(states), QList{Core::Player::State::Stopped});
    }

    void aTrackThatCannotPlayAtTheEndIsOneChangeOfState() {  // ERR-07
        Stack stack;
        QVERIFY(stack.start(1));
        QSignalSpy states(&stack.player, &Core::Player::stateChanged);
        stack.player.playIndex(0);
        QTRY_COMPARE(States(states), QList{Core::Player::State::Loading});
        states.clear();
        stack.source.fail(0, Core::FailureKind::Track);
        QTest::qWait(50);
        QCOMPARE(States(states), QList{Core::Player::State::Stopped});
    }

    void aLostNetworkIsOneChangeOfState() {  // ERR-01, ERR-03
        Stack stack;
        QVERIFY(stack.start(2));
        QSignalSpy states(&stack.player, &Core::Player::stateChanged);
        stack.player.playIndex(0);
        QTRY_COMPARE(States(states), QList{Core::Player::State::Loading});
        states.clear();
        stack.source.fail(0, Core::FailureKind::Network);
        QTest::qWait(50);
        QCOMPARE(States(states), QList{Core::Player::State::WaitingForNetwork});

        stack.player.playIndex(1);
        const QPointer<Tests::FakeDownload> cut = stack.source.complete(1);
        cut->feed(mp3.left(6'000));
        cut->finish(Core::TrackFailure{Core::FailureKind::Network, QStringLiteral("cut")});
        QTRY_COMPARE(stack.player.state(), Core::Player::State::Playing);
        QTRY_VERIFY(States(states).last() == Core::Player::State::Playing);
        states.clear();
        QTRY_COMPARE_WITH_TIMEOUT(stack.player.state(), Core::Player::State::WaitingForNetwork, 3'000);
        QTest::qWait(50);
        QCOMPARE(States(states), QList{Core::Player::State::WaitingForNetwork});
    }

    void anUndecodableLastTrackIsOneChangeOfState() {  // ERR-04, ERR-07
        Stack stack;
        QVERIFY(stack.start(1));
        QSignalSpy states(&stack.player, &Core::Player::stateChanged);
        stack.player.playIndex(0);
        QTRY_COMPARE(States(states), QList{Core::Player::State::Loading});
        states.clear();
        const QPointer<Tests::FakeDownload> broken = stack.source.complete(0);
        broken->feed(QByteArray(40'000, 'x'));
        broken->finish();
        QTRY_COMPARE_WITH_TIMEOUT(stack.player.state(), Core::Player::State::Stopped, 5'000);
        QTest::qWait(50);
        QCOMPARE(States(states), QList{Core::Player::State::Stopped});
    }

    void withoutAnOutputDevicePlayStaysStopped() {
        Stack stack;
        stack.player.setSource(HeadOf(Tracks(2)), false);
        QSignalSpy states(&stack.player, &Core::Player::stateChanged);
        QTRY_COMPARE(States(states), QList{Core::Player::State::Stopped});
        states.clear();
        QList<Core::PlayerNotice> notices;
        connect(&stack.player, &Core::Player::noticed, this, [&](const Core::PlayerNotice& notice) {
            notices << notice;
        });
        stack.player.play();
        QTest::qWait(50);
        QVERIFY(states.isEmpty());
        QCOMPARE(stack.player.state(), Core::Player::State::Stopped);
        QCOMPARE(stack.player.currentIndex(), 0);
        QVERIFY(stack.source.asked().isEmpty());
        QVERIFY(stack.events.isEmpty());
        QCOMPARE(notices.size(), 1);
        QCOMPARE(notices.first().code, Core::NoticeCode::NoOutputDevice);
    }

    void aContinuationHearsOnlyTheTracksItGaveAlsoAfterTheQueueWasReplaced() {  // TRK-03 to TRK-06
        Stack stack;
        QVERIFY(stack.start(0));
        QList<Core::Track> given = Tracks(2);
        for (Core::Track& track : given) {
            track.key.id = QStringLiteral("w") + track.key.id;
        }
        const auto vibe = std::make_shared<Tests::FakeContinuation>();
        stack.player.setSource(
            Core::
                QueueSource{.title = QStringLiteral("Mixed"), .head = Tracks(1), .continuation = vibe, .given = given},
            false
        );
        QCOMPARE(vibe->begins(), QList<bool>{true});
        stack.player.playIndex(0);
        stack.source.complete(0);
        stack.player.playIndex(1);
        stack.source.complete(1);
        QCOMPARE(vibe->eventLog(), QStringList{"started:w1"});
        stack.player.setSource(HeadOf(Tracks(3)), false);
        QCOMPARE(vibe->eventLog(), (QStringList{"started:w1", "skipped:w1"}));
        stack.player.playIndex(0);
        stack.source.complete(2);
        stack.player.stop();
        QCOMPARE(vibe->eventLog().size(), 2);
        QCOMPARE(
            stack.events, (QStringList{"started:1", "skipped:1", "started:w1", "skipped:w1", "started:1", "skipped:1"})
        );
    }

    void theSourceHearsTheEventsOfItsTracks() {
        Stack stack;
        QVERIFY(stack.start(2));
        stack.player.playIndex(0);
        stack.source.complete(0);
        stack.player.playIndex(1);
        stack.source.complete(1);
        stack.player.stop();
        const QStringList expected{"started:1", "skipped:1", "started:2", "skipped:2"};
        QCOMPARE(stack.source.eventLog(), expected);
        QCOMPARE(stack.events, expected);
    }
};

QTEST_GUILESS_MAIN(TestPlayer)
#include "player_test.moc"
