#include "audio/audio_engine.h"
#include "core/player.h"
#include "core/track.h"
#include "core/track_source.h"
#include "support/fake_source.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTest>

#include <functional>

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

QString EventName(Core::TrackEvent event) {
    switch (event) {
        case Core::TrackEvent::Started: return QStringLiteral("started");
        case Core::TrackEvent::Finished: return QStringLiteral("finished");
        case Core::TrackEvent::Skipped: break;
    }
    return QStringLiteral("skipped");
}

Core::Player::TEventCallback Recorder(QStringList& log) {
    return [&log](Core::TrackEvent event, const Core::Track& track, double) {
        log << EventName(event) + u':' + track.key.id;
    };
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
    }
    bool start(int tracks) {
        if (!engine.init().ok) {
            return false;
        }
        player.setQueue(Tracks(tracks), QStringLiteral("Fake"), false, {}, Recorder(events));
        return true;
    }
};

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

    void aRetryAfterTheNetworkWaitIsDroppedAndNoneFollowsAStop() {  // ERR-01
        Stack stack;
        QVERIFY(stack.start(2));
        stack.player.setNetworkRetryDelays(50, 50);
        stack.player.playIndex(0);
        stack.source.fail(0, Core::FailureKind::Network);
        QVERIFY(stack.player.isWaitingForNetwork());
        QVERIFY(QTest::qWaitFor([&] { return stack.source.asked().size() == 2; }, 2'000));
        stack.player.stop();
        QVERIFY(!stack.player.isWaitingForNetwork());
        QVERIFY(!stack.source.complete(1));
        QVERIFY(stack.source.started().isEmpty());
        QVERIFY(stack.events.isEmpty());
        QTest::qWait(300);
        QCOMPARE(stack.source.asked().size(), 2);
        QCOMPARE(stack.source.pending(), 0);
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
        std::function<void(const QList<Core::Track>&)> late;
        stack.player.setQueue(Tracks(1), QStringLiteral("Vibe"), false, [&](auto done) { late = done; });
        stack.player.playIndex(0);
        QVERIFY(late);
        stack.player.setQueue(Tracks(2), QStringLiteral("Other"), false);
        late(Tracks(3));
        QCOMPARE(stack.player.playlist().size(), 2);
        late = {};
        stack.player.changeSource(QStringLiteral("Vibe"), [&](auto done) { late = done; }, {}, {});
        stack.player.requestMore();
        QVERIFY(late);
        stack.player.changeSource(QStringLiteral("Fixed"), {}, {}, {});
        late(Tracks(3));
        QCOMPARE(stack.player.playlist().size(), 2);
    }

    void aTrackStartsUnderTheRulesOfItsStart() {  // HOST-15
        Stack stack;
        QVERIFY(stack.start(2));
        stack.player.playIndex(0);
        QStringList jamEvents;
        Core::QueueRules jamRules;
        jamRules.playReports = false;
        stack.player.changeSource(QStringLiteral("Jam"), {}, Recorder(jamEvents), jamRules);
        stack.source.complete(0);
        QCOMPARE(jamEvents, QStringList{"started:1"});
        QVERIFY(stack.events.isEmpty());
        QVERIFY(stack.source.events().isEmpty());
        stack.player.stop();
        QCOMPARE(jamEvents, (QStringList{"started:1", "skipped:1"}));
        QVERIFY(stack.source.events().isEmpty());
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
