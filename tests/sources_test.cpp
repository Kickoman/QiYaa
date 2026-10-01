#include "audio/audio_engine.h"
#include "core/player.h"
#include "core/sources.h"
#include "support/mock_http_server.h"
#include "support/spec_fixtures.h"
#include "yandex/api_client.h"
#include "yandex/library.h"

#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVariant>

// Core::Sources against the spec/player scenarios, on the spec API fixtures. Each test names the
// scenarios it checks.

namespace {

QString ExpectedTitle(const QString& name, int index = 0) {
    return Tests::ExpectedObject(name)
        .value(QStringLiteral("tracks"))
        .toArray()
        .at(index)
        .toObject()
        .value(QStringLiteral("title"))
        .toString();
}

Yandex::Track KeptTrack() {
    Yandex::Track track;
    track.id = QStringLiteral("1");
    track.title = QStringLiteral("Kept");
    return track;
}

}  // namespace

class TestSources : public QObject {
    Q_OBJECT
private:
    Tests::MockHttpServer server;
    Tests::LocalNetworkAccessManager networkManager;
    Yandex::ApiClient api{&networkManager};
    Yandex::Library library{&api};

    // A player without a sound card, and its sources: the queue logic only.
    struct Stack {
        Audio::AudioEngine engine;  // not initialised
        Core::Player player;
        Core::Sources sources;
        QSignalSpy status;
        explicit Stack(Yandex::Library* library)
            : player(library, &engine)
            , sources(&player, library)
            , status(&player, &Core::Player::statusMessage) { }
        bool saw(const QString& text) const {
            for (const QList<QVariant>& arguments : status) {
                if (arguments.at(0).toString() == text) {
                    return true;
                }
            }
            return false;
        }
        bool sawPrefix(const QString& prefix) const {
            for (const QList<QVariant>& arguments : status) {
                if (arguments.at(0).toString().startsWith(prefix)) {
                    return true;
                }
            }
            return false;
        }
    };

    QList<QJsonObject> feedbackEvents(qsizetype from) const {
        QList<QJsonObject> events;
        for (qsizetype i = from; i < server.requests().size(); ++i) {
            const Tests::MockRequest& request = server.requests()[i];
            if (request.path.startsWith(QStringLiteral("/rotor/session/"))
                && request.path.endsWith(QStringLiteral("/feedback"))) {
                QJsonObject body = QJsonDocument::fromJson(request.body).object();
                QJsonObject event = body.value(QStringLiteral("event")).toObject();
                event.insert(QStringLiteral("batchId"), body.value(QStringLiteral("batchId")));
                events << event;
            }
        }
        return events;
    }

private Q_SLOTS:
    void initTestCase() {
        api.setBaseUrl(server.baseUrl());
        api.setToken(QStringLiteral("test-token"));
        server.fixture("GET", "/account/status", "account-status/ok");
        // Every test's wave is the same session. Answered from the start: a 404 would move the
        // session to the station endpoint for good (TRK-10).
        server.fixture(
            "POST",
            QStringLiteral("/rotor/session/%1/feedback")
                .arg(Tests::ExpectedObject("rotor-session-new/ok").value("sessionId").toString()),
            "rotor-session-feedback/ok"
        );
        bool done = false;
        library.connectAccount([&](const Yandex::Account&, const QString&) { done = true; });
        QVERIFY(QTest::qWaitFor([&] { return done; }, 5000));
        QVERIFY(library.isLoggedIn());
    }

    void lastPickedSourceWins() {  // SRC-01
        Stack stack(&library);
        server.fixture("GET", "/users/42/likes/tracks", "users-likes-tracks/string-ids", 300);
        server.fixture("POST", "/tracks/", "tracks/two-tracks");
        server.fixture("POST", "/rotor/session/new", "rotor-session-new/ok");
        stack.sources.playLikes(false);
        stack.sources.playMyWave();
        QVERIFY(QTest::qWaitFor(
            [&] { return stack.player.queueTitle() == QStringLiteral("My Vibe"); }, 3000
        ));
        QTest::qWait(600);  // the likes response arrives now
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("My Vibe"));
        QCOMPARE(stack.player.playlist().first().title, ExpectedTitle("rotor-session-new/ok"));
        QVERIFY(!stack.saw(QStringLiteral("Liked: 2 track(s)")));
    }

    void staleFailureIsSilent() {  // SRC-02
        Stack stack(&library);
        server.fixture(
            "GET", "/users/42/likes/tracks", "users-likes-artists/401-session-expired", 300
        );
        server.fixture("POST", "/rotor/session/new", "rotor-session-new/ok");
        stack.sources.playLikes(true);
        stack.sources.playMyWave();
        QVERIFY(QTest::qWaitFor(
            [&] { return stack.player.queueTitle() == QStringLiteral("My Vibe"); }, 3000
        ));
        QTest::qWait(600);
        QVERIFY(!stack.sawPrefix(QStringLiteral("Error")));
    }

    void searchIsOnePickAcrossItsRequests() {  // SRC-03
        Stack stack(&library);
        server.fixture("GET", "/search", "search/best-artist");
        server.fixture(
            "GET", "/artists/9/track-ids-by-rating", "artists-track-ids-by-rating/ok", 400
        );
        server.fixture("POST", "/tracks/", "tracks/two-tracks");
        server.fixture("POST", "/rotor/session/new", "rotor-session-new/ok");
        const auto before = server.requests().size();
        stack.sources.search(QStringLiteral("кино"));
        QVERIFY(QTest::qWaitFor(
            [&] {
                for (qsizetype i = before; i < server.requests().size(); ++i) {
                    if (server.requests()[i].path == "/artists/9/track-ids-by-rating") {
                        return true;
                    }
                }
                return false;
            },
            3000
        ));
        stack.sources.playMyWave();
        QVERIFY(QTest::qWaitFor(
            [&] { return stack.player.queueTitle() == QStringLiteral("My Vibe"); }, 3000
        ));
        QTest::qWait(800);  // the artist's tracks arrive now
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("My Vibe"));
    }

    void failedSourceKeepsTheQueue() {  // SRC-04
        Stack stack(&library);
        stack.player.setQueue({KeptTrack()}, "Kept", false);
        server.fixture("GET", "/albums/4053/with-tracks", "account-status/500-empty");
        stack.sources.playAlbum(QStringLiteral("4053"), QStringLiteral("Звезда"));
        QVERIFY(QTest::qWaitFor([&] { return stack.sawPrefix(QStringLiteral("Error: ")); }, 3000));
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("Kept"));
    }

    void likesReplaceTheQueueAndCountTheTracks() {  // SRC-06
        Stack stack(&library);
        server.fixture("GET", "/users/42/likes/tracks", "users-likes-tracks/string-ids");
        server.fixture("POST", "/tracks/", "tracks/two-tracks");
        stack.sources.playLikes(false);
        QVERIFY(
            QTest::qWaitFor([&] { return stack.saw(QStringLiteral("Liked: 2 track(s)")); }, 3000)
        );
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("Liked"));
        QCOMPARE(stack.player.playlist().size(), 2);
        QCOMPARE(stack.player.playlist().first().title, ExpectedTitle("tracks/two-tracks"));
    }

    void emptySourceKeepsTheQueue_data() {  // SRC-07, SRC-08
        QTest::addColumn<QString>("likes");
        QTest::addColumn<QString>("tracks");
        QTest::newRow("no likes") << QStringLiteral("users-likes-tracks/empty")
                                  << QStringLiteral("tracks/two-tracks");
        QTest::newRow("all unavailable") << QStringLiteral("users-likes-tracks/string-ids")
                                         << QStringLiteral("tracks/all-unavailable");
    }
    void emptySourceKeepsTheQueue() {
        QFETCH(QString, likes);
        QFETCH(QString, tracks);
        Stack stack(&library);
        stack.player.setQueue({KeptTrack()}, "Kept", false);
        server.fixture("GET", "/users/42/likes/tracks", likes);
        server.fixture("POST", "/tracks/", tracks);
        stack.sources.playLikes(true);
        QVERIFY(QTest::qWaitFor([&] { return stack.saw(QStringLiteral("Liked: empty")); }, 3000));
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("Kept"));

        server.fixture("GET", "/artists/9/track-ids-by-rating", "artists-track-ids-by-rating/ok");
        stack.sources.playArtist(QStringLiteral("9"), QStringLiteral("Кино"));
        QVERIFY(QTest::qWaitFor(
            [&] {
                return stack.saw(QStringLiteral("Кино: empty"))
                    || stack.player.queueTitle() == QStringLiteral("Кино");
            },
            3000
        ));
        QCOMPARE(
            stack.player.queueTitle(),
            tracks == QStringLiteral("tracks/all-unavailable") ? QStringLiteral("Kept")
                                                               : QStringLiteral("Кино")
        );
    }

    void waveWithoutPlayableTracksKeepsTheQueue() {  // WAVE-03
        Stack stack(&library);
        stack.player.setQueue({KeptTrack()}, "Kept", false);
        server.fixture("POST", "/rotor/session/new", "rotor-session-new/all-unavailable");
        const auto before = server.requests().size();
        stack.sources.playMyWave();
        QVERIFY(QTest::qWaitFor([&] { return stack.saw(QStringLiteral("My Vibe: empty")); }, 3000));
        QTest::qWait(100);
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("Kept"));
        QVERIFY(feedbackEvents(before).isEmpty());  // no radioStarted for a wave that never starts
    }

    void searchQueuesTheBestResult_data() {  // SRC-09 to SRC-12
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("title");
        QTest::addColumn<int>("count");
        QTest::newRow("artist") << QStringLiteral("search/best-artist") << QStringLiteral("Кино")
                                << 2;
        QTest::newRow("album") << QStringLiteral("search/best-album")
                               << QStringLiteral("Звезда по имени Солнце") << 3;
        QTest::newRow("track") << QStringLiteral("search/best-track")
                               << QStringLiteral("Search: кино") << 1;
        QTest::newRow("playlist") << QStringLiteral("search/best-playlist")
                                  << QStringLiteral("Search: кино") << 1;
        QTest::newRow("nothing") << QStringLiteral("search/no-best") << QString() << 0;
    }
    void searchQueuesTheBestResult() {
        QFETCH(QString, name);
        QFETCH(QString, title);
        QFETCH(int, count);
        Stack stack(&library);
        stack.player.setQueue({KeptTrack()}, "Kept", false);
        server.fixture("GET", "/search", name);
        server.fixture("GET", "/artists/9/track-ids-by-rating", "artists-track-ids-by-rating/ok");
        server.fixture("POST", "/tracks/", "tracks/two-tracks");
        server.fixture("GET", "/albums/4053/with-tracks", "albums-with-tracks/two-volumes");
        stack.sources.search(QStringLiteral("кино"));
        if (title.isEmpty()) {
            QVERIFY(
                QTest::qWaitFor([&] { return stack.saw(QStringLiteral("Nothing found")); }, 3000)
            );
            QCOMPARE(stack.player.queueTitle(), QStringLiteral("Kept"));
            return;
        }
        QVERIFY(QTest::qWaitFor([&] { return stack.player.queueTitle() == title; }, 3000));
        QCOMPARE(stack.player.playlist().size(), count);
    }

    // WAVE-01, WAVE-02, WAVE-05, WAVE-08, TRK-03, TRK-04, TRK-06, TRK-07: a wave played through
    // the real engine on the Null output.
    void waveStartsLoadsMoreAndReportsEveryTrack() {
        QFile file(QStringLiteral(QIYAA_TEST_DATA "/sine440_3s.mp3"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        Stack stack(&library);
        if (!stack.engine.init().ok) {
            QSKIP("no audio output");
        }
        stack.engine.setVolume(0);
        server.audioTracks(
            {QStringLiteral("38634572"), QStringLiteral("38634573")}, file.readAll()
        );
        server.fixture("POST", "/rotor/session/new", "rotor-session-new/ok");
        const QJsonObject first = Tests::ExpectedObject("rotor-session-new/ok");
        const QJsonObject more = Tests::ExpectedObject("rotor-session-tracks/ok");
        const QString session = first.value("sessionId").toString();
        server.fixture(
            "POST", QStringLiteral("/rotor/session/%1/tracks").arg(session),
            "rotor-session-tracks/ok"
        );
        server.fixture(
            "POST", "/users/42/dislikes/tracks/add-multiple",
            "users-dislikes-tracks-add-multiple/ok"
        );
        const auto before = server.requests().size();

        stack.sources.playMyWave();
        QVERIFY(QTest::qWaitFor([&] { return feedbackEvents(before).size() >= 2; }, 8000));
        const QJsonObject newBody =
            QJsonDocument::fromJson(server.last("/rotor/session/new")->body).object();
        QCOMPARE(newBody.value("seeds").toArray(), QJsonArray{"user:onyourwave"});  // WAVE-02
        QCOMPARE(stack.sources.lastWaveSeeds(), QStringList{"user:onyourwave"});
        // The unavailable track of the batch is left out (SRC-05).
        for (const Yandex::Track& track : stack.player.playlist()) {
            QVERIFY(track.available);
        }

        const QString firstId = stack.player.playlist().first().id;
        const QString firstTrack = firstId + u':' + stack.player.playlist().first().albumId;
        QList<QJsonObject> events = feedbackEvents(before);
        QCOMPARE(events[0].value("type").toString(), QStringLiteral("radioStarted"));  // TRK-03
        QCOMPARE(events[0].value("batchId").toString(), first.value("batchId").toString());
        QCOMPARE(events[1].value("type").toString(), QStringLiteral("trackStarted"));  // TRK-04
        QCOMPARE(events[1].value("trackId").toString(), firstTrack);
        QCOMPARE(events[1].value("batchId").toString(), first.value("batchId").toString());

        // One track left: loading more was asked for with the queue's last ids (WAVE-05).
        QVERIFY(QTest::qWaitFor(
            [&] { return server.last(QStringLiteral("/rotor/session/%1/tracks").arg(session)); },
            3000
        ));
        const QJsonObject moreBody =
            QJsonDocument::fromJson(
                server.last(QStringLiteral("/rotor/session/%1/tracks").arg(session))->body
            )
                .object();
        QCOMPARE(moreBody.value("queue").toArray(), QJsonArray{firstId});

        // Dislike: the request, then a skip and the next track, from the new batch (TRK-07).
        QVERIFY(QTest::qWaitFor([&] { return stack.player.playlist().size() == 2; }, 3000));
        stack.sources.dislikeAndSkip(firstId);
        QVERIFY(QTest::qWaitFor([&] { return feedbackEvents(before).size() >= 4; }, 5000));
        QCOMPARE(
            server.last("/users/42/dislikes/tracks/add-multiple")->formValue("track-ids"), firstId
        );
        events = feedbackEvents(before);
        QCOMPARE(events[2].value("type").toString(), QStringLiteral("skip"));  // TRK-06
        QCOMPARE(events[2].value("trackId").toString(), firstTrack);
        QCOMPARE(events[3].value("type").toString(), QStringLiteral("trackStarted"));
        QCOMPARE(events[3].value("batchId").toString(), more.value("batchId").toString());
        QCOMPARE(stack.player.currentIndex(), 1);
        stack.player.stop();
    }
};

QTEST_GUILESS_MAIN(TestSources)
#include "sources_test.moc"
