#include "audio/audio_engine.h"
#include "core/jam_mode.h"
#include "core/player.h"
#include "core/sources.h"
#include "support/mock_http_server.h"
#include "support/spec_fixtures.h"
#include "yandex/api_client.h"
#include "yandex/library.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>

// Core::JamMode against spec/jam/host.md, by scenario ID. The player has no sound card: a track
// opens once its link resolves, and only Next moves on.

namespace {

const QString kFirstWaveTrack = QStringLiteral("38634572");
const QString kMoreWaveTrack = QStringLiteral("38634573");

Yandex::Track TrackOf(const QString& id) {
    Yandex::Track track;
    track.id = id;
    track.albumId = QStringLiteral("1");
    track.title = QStringLiteral("T") + id;
    track.artists = {QStringLiteral("A")};
    track.durationMs = 3'000;
    return track;
}

Core::JamEntry Item(const QString& itemId, const QString& trackId) {
    return Core::JamEntry{itemId, TrackOf(trackId), QStringLiteral("g1")};
}

QStringList Ids(const Core::Player& player) {
    QStringList ids;
    for (const Yandex::Track& track : player.playlist()) {
        ids << track.id;
    }
    return ids;
}

}  // namespace

class TestJamMode : public QObject {
    Q_OBJECT
private:
    Tests::MockHttpServer server;
    Tests::LocalNetworkAccessManager networkManager;
    Yandex::ApiClient api{&networkManager};
    Yandex::Library library{&api};

    struct Stack {
        Audio::AudioEngine engine;  // not initialised: no sound card
        Core::Player player;
        Core::Sources sources;
        Core::JamMode jam;
        QSignalSpy started;
        QSignalSpy playbacks;
        QSignalSpy status;
        explicit Stack(Yandex::Library* library)
            : player(library, &engine)
            , sources(&player, library)
            , jam(&player, library)
            , started(&jam, &Core::JamMode::itemStarted)
            , playbacks(&jam, &Core::JamMode::playback)
            , status(&player, &Core::Player::statusMessage) {
            sources.setJamMode(&jam);
        }
        Core::JamPlayback lastPlayback() const {
            return playbacks.last().at(0).value<Core::JamPlayback>();
        }
    };

    QList<const Tests::MockRequest*> requestsTo(const QString& path, qsizetype from = 0) const {
        QList<const Tests::MockRequest*> found;
        for (qsizetype i = from; i < server.requests().size(); ++i) {
            if (server.requests()[i].path == path) {
                found << &server.requests()[i];
            }
        }
        return found;
    }

    QJsonArray seedsOf(const Tests::MockRequest* request) const {
        return QJsonDocument::fromJson(request->body)
            .object()
            .value(QStringLiteral("seeds"))
            .toArray();
    }

    QList<QJsonObject> feedback(qsizetype from) const {
        QList<QJsonObject> events;
        for (qsizetype i = from; i < server.requests().size(); ++i) {
            const Tests::MockRequest& request = server.requests()[i];
            if (request.path.startsWith(QStringLiteral("/rotor/session/"))
                && request.path.endsWith(QStringLiteral("/feedback"))) {
                events << QJsonDocument::fromJson(request.body)
                              .object()
                              .value(QStringLiteral("event"))
                              .toObject();
            }
        }
        return events;
    }

    void waveAnswers() {
        server.fixture("POST", "/rotor/session/new", "rotor-session-new/ok");
        server.fixture(
            "POST", QStringLiteral("/rotor/session/%1/tracks").arg(QStringLiteral("S1-onyourwave")),
            "rotor-session-tracks/ok"
        );
    }

    void noWave() {
        server.fixture("POST", "/rotor/session/new", "rotor-session-new/all-unavailable");
    }

private Q_SLOTS:
    void initTestCase() {
        qRegisterMetaType<Core::JamPlayback>();
        api.setBaseUrl(server.baseUrl());
        api.setToken(QStringLiteral("test-token"));
        server.fixture("GET", "/account/status", "account-status/ok");
        server.fixture(
            "POST", QStringLiteral("/rotor/session/S1-onyourwave/feedback"),
            "rotor-session-feedback/ok"
        );
        QFile file(QStringLiteral(QIYAA_TEST_DATA "/sine440_3s.mp3"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        server.audioTracks(
            {QStringLiteral("11"), QStringLiteral("22"), QStringLiteral("33"), QStringLiteral("44"),
             QStringLiteral("55"), QStringLiteral("66"), kFirstWaveTrack, kMoreWaveTrack},
            file.readAll()
        );
        bool done = false;
        library.connectAccount([&](const Yandex::Account&, const QString&) { done = true; });
        QVERIFY(QTest::qWaitFor([&] { return done; }, 5000));
    }

    void startKeepsThePlayingTrackAndDropsTheRest() {  // HOST-14
        Stack stack(&library);
        noWave();
        stack.player.setQueue(
            {TrackOf("11"), TrackOf("22"), TrackOf("33"), TrackOf("44")}, QStringLiteral("Likes"),
            false
        );
        stack.player.playIndex(1);
        stack.jam.start(QStringLiteral("Джем"), false);
        QCOMPARE(Ids(stack.player), QStringList{QStringLiteral("22")});
        QCOMPARE(stack.jam.queueSlots().size(), 1);
        QCOMPARE(stack.jam.currentSlot().kind, Core::JamSlot::Kind::Other);
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("Джем"));
        QVERIFY(!stack.player.rules().playReports);
        QCOMPARE(stack.lastPlayback().kind, Core::JamPlayback::Kind::Wave);
        QCOMPARE(stack.lastPlayback().track->id, QStringLiteral("22"));
    }

    void theJamPartFollowsTheStateWithTheSmallestChange() {  // HOST-01 to HOST-04
        Stack stack(&library);
        noWave();
        stack.jam.start(QStringLiteral("Джем"), false);
        stack.jam.setQueue({Item("i1", "11"), Item("i2", "22"), Item("i3", "33")}, {}, 0);
        QCOMPARE(Ids(stack.player), (QStringList{"11", "22", "33"}));
        QTRY_COMPARE(stack.started.size(), 1);
        QSignalSpy changes(&stack.player, &Core::Player::playlistChanged);
        stack.jam.setQueue(
            {Item("i1", "11"), Item("i2", "22"), Item("i3", "33")}, {}, 0
        );  // HOST-04
        QCOMPARE(Ids(stack.player), (QStringList{"11", "22", "33"}));
        QCOMPARE(changes.size(), 0);
        stack.jam.setQueue(
            {Item("i2", "22"), Item("i4", "44"), Item("i5", "55")}, {}, 0
        );  // HOST-01
        QCOMPARE(Ids(stack.player), (QStringList{"11", "22", "44", "55"}));
        QCOMPARE(stack.jam.queueSlots()[1].itemId, QStringLiteral("i2"));
        stack.jam.setQueue({Item("i4", "44"), Item("i2", "22")}, {}, 0);  // HOST-03
        QCOMPARE(Ids(stack.player), (QStringList{"11", "44", "22"}));
        QCOMPARE(stack.player.currentIndex(), 0);
    }

    void theNextItemReportsStartedThenPlaying() {  // HOST-05, HOST-06
        Stack stack(&library);
        noWave();
        stack.jam.setPlayingReportInterval(50);
        stack.jam.start(QStringLiteral("Джем"), false);
        stack.jam.setQueue({Item("i1", "11"), Item("i2", "22")}, {}, 0);
        QTRY_COMPARE(stack.started.size(), 1);
        QCOMPARE(stack.started.at(0).at(0).toString(), QStringLiteral("i1"));
        stack.player.next();
        QTRY_COMPARE(stack.started.size(), 2);
        QCOMPARE(stack.started.at(1).at(0).toString(), QStringLiteral("i2"));
        QTRY_COMPARE(stack.lastPlayback().itemId, QStringLiteral("i2"));
        QCOMPARE(stack.lastPlayback().kind, Core::JamPlayback::Kind::Item);
    }

    void previousRestartsTheTrack() {  // HOST-07
        Stack stack(&library);
        noWave();
        stack.jam.start(QStringLiteral("Джем"), false);
        stack.jam.setQueue({Item("i1", "11"), Item("i2", "22")}, {}, 0);
        stack.player.next();
        QCOMPARE(stack.player.currentIndex(), 1);
        stack.player.previous();
        QCOMPARE(stack.player.currentIndex(), 1);
    }

    void anItemArrivingWhileNothingPlaysStartsAtOnce() {  // HOST-08
        Stack stack(&library);
        noWave();
        stack.jam.start(QStringLiteral("Джем"), false);
        stack.jam.setQueue({Item("i1", "11")}, {}, 0);
        QTRY_COMPARE(stack.started.size(), 1);
        stack.player.next();
        QTRY_VERIFY(stack.player.isWaitingForMore() && !stack.player.trackInProgress());
        QTest::qWait(200);
        QVERIFY(stack.player.isWaitingForMore());
        QCOMPARE(stack.lastPlayback().kind, Core::JamPlayback::Kind::Idle);
        stack.jam.setQueue({Item("i2", "22")}, {}, 0);
        QCOMPARE(stack.player.currentIndex(), 1);
        QTRY_COMPARE(stack.started.size(), 2);
        QCOMPARE(stack.started.at(1).at(0).toString(), QStringLiteral("i2"));
    }

    void theJamWaveStartsFromTheSeedsAndWaitsBehindTheItems() {  // HOST-10, HOST-12
        Stack stack(&library);
        waveAnswers();
        const qsizetype before = server.requests().size();
        stack.jam.start(QStringLiteral("Джем"), false);
        stack.jam.setQueue({Item("i1", "11")}, {QStringLiteral("track:11")}, 1);
        QTRY_COMPARE(Ids(stack.player), (QStringList{"11", kFirstWaveTrack}));
        const auto starts = requestsTo(QStringLiteral("/rotor/session/new"), before);
        QCOMPARE(starts.size(), 1);
        QCOMPARE(seedsOf(starts.first()), QJsonArray{QStringLiteral("track:11")});
        QCOMPARE(stack.jam.queueSlots()[1].kind, Core::JamSlot::Kind::Wave);
        stack.jam.setQueue({Item("i1", "11"), Item("i2", "22")}, {QStringLiteral("track:11")}, 1);
        QCOMPARE(Ids(stack.player), (QStringList{"11", "22", kFirstWaveTrack}));
    }

    void newSeedsDropTheDeferredWaveWhenTheItemsRunOut() {  // HOST-11
        Stack stack(&library);
        waveAnswers();
        const qsizetype before = server.requests().size();
        stack.jam.start(QStringLiteral("Джем"), false);
        stack.jam.setQueue({Item("i1", "11")}, {QStringLiteral("track:11")}, 1);
        QTRY_COMPARE(Ids(stack.player), (QStringList{"11", kFirstWaveTrack}));
        const QStringList newSeeds = {QStringLiteral("track:22"), QStringLiteral("track:11")};
        stack.jam.setQueue({Item("i1", "11"), Item("i2", "22")}, newSeeds, 2);
        QCOMPARE(Ids(stack.player), (QStringList{"11", "22", kFirstWaveTrack}));
        stack.player.next();
        QTRY_COMPARE(requestsTo(QStringLiteral("/rotor/session/new"), before).size(), 2);
        QCOMPARE(
            seedsOf(requestsTo(QStringLiteral("/rotor/session/new"), before).last()),
            QJsonArray::fromStringList(newSeeds)
        );
        QTRY_COMPARE(Ids(stack.player), (QStringList{"11", "22", kFirstWaveTrack}));
        QCOMPARE(stack.jam.queueSlots()[2].kind, Core::JamSlot::Kind::Wave);
    }

    void withoutSeedsTheWaveStartsFromTheCurrentTrack() {  // HOST-13
        Stack stack(&library);
        waveAnswers();
        stack.jam.start(QStringLiteral("Джем"), false);
        QTRY_COMPARE(stack.lastPlayback().kind, Core::JamPlayback::Kind::Idle);
        const qsizetype before = server.requests().size();
        stack.jam.setQueue({Item("i1", "11")}, {}, 0);
        QTRY_COMPARE(requestsTo(QStringLiteral("/rotor/session/new"), before).size(), 1);
        QCOMPARE(
            seedsOf(requestsTo(QStringLiteral("/rotor/session/new"), before).first()),
            QJsonArray{QStringLiteral("track:11")}
        );
    }

    void noPlayReportsAndFeedbackOnlyToTheJamWave() {  // HOST-15, HOST-16
        Stack stack(&library);
        waveAnswers();
        const qsizetype before = server.requests().size();
        stack.jam.start(QStringLiteral("Джем"), true);
        stack.jam.setQueue({Item("i1", "11")}, {QStringLiteral("track:11")}, 1);
        QTRY_COMPARE(Ids(stack.player).size(), 2);
        QTRY_COMPARE(stack.started.size(), 1);
        stack.player.next();
        QTRY_VERIFY(feedback(before).size() >= 2);
        const QList<QJsonObject> events = feedback(before);
        QCOMPARE(events[0].value("type").toString(), QStringLiteral("radioStarted"));
        QCOMPARE(events[1].value("type").toString(), QStringLiteral("trackStarted"));
        QVERIFY(events[1].value("trackId").toString().startsWith(kFirstWaveTrack));
        QVERIFY(requestsTo(QStringLiteral("/play-audio"), before).isEmpty());

        Stack quiet(&library);
        const qsizetype quietBefore = server.requests().size();
        quiet.jam.start(QStringLiteral("Джем"), false);
        quiet.jam.setQueue({Item("i1", "11")}, {QStringLiteral("track:11")}, 1);
        QTRY_COMPARE(Ids(quiet.player).size(), 2);
        quiet.player.next();
        QTest::qWait(300);
        QVERIFY(feedback(quietBefore).isEmpty());
    }

    void aSkipCommandMovesOnOnlyForTheCurrentItem() {  // HOST-18, HOST-19
        Stack stack(&library);
        noWave();
        stack.jam.start(QStringLiteral("Джем"), false);
        stack.jam.setQueue({Item("i1", "11"), Item("i2", "22")}, {}, 0);
        stack.jam.skip(QStringLiteral("i2"));
        QCOMPARE(stack.player.currentIndex(), 0);
        stack.jam.skip(QStringLiteral("i1"));
        QCOMPARE(stack.player.currentIndex(), 1);
    }

    void aPickDoesNotReplaceTheQueueDuringTheJam() {  // HOST-21
        Stack stack(&library);
        noWave();
        stack.jam.start(QStringLiteral("Джем"), false);
        stack.jam.setQueue({Item("i1", "11")}, {}, 0);
        stack.sources.playMyWave();
        stack.sources.search(QStringLiteral("кино"));
        QCOMPARE(Ids(stack.player), QStringList{QStringLiteral("11")});
        QCOMPARE(
            stack.status.last().at(0).toString(),
            QStringLiteral("Идёт джем: добавляйте треки в джем")
        );
    }

    void theEndKeepsTheItemsDropsTheWaveAndReportsPlaysAgain() {  // HOST-32, HOST-33
        Stack stack(&library);
        waveAnswers();
        stack.jam.start(QStringLiteral("Джем"), false);
        stack.jam.setQueue({Item("i1", "11"), Item("i2", "22")}, {QStringLiteral("track:11")}, 1);
        stack.player.next();
        QTRY_COMPARE(Ids(stack.player), (QStringList{"11", "22", kFirstWaveTrack}));
        stack.player.previous();
        QCOMPARE(stack.player.currentIndex(), 1);
        stack.jam.end();
        QVERIFY(!stack.jam.isActive());
        QCOMPARE(Ids(stack.player), (QStringList{"11", "22"}));
        QVERIFY(stack.player.rules().playReports);
        const qsizetype before = server.requests().size();
        stack.player.playIndex(0);
        QTRY_COMPARE(requestsTo(QStringLiteral("/play-audio"), before).size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestJamMode)
#include "jam_mode_test.moc"
