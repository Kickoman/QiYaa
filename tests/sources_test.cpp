#include "audio/audio_engine.h"
#include "core/player.h"
#include "core/queue_source.h"
#include "core/search.h"
#include "core/source_notice.h"
#include "sources/yandex/catalog.h"
#include "sources/yandex/history_wave.h"
#include "sources/yandex/tracks.h"
#include "support/mock_http_server.h"
#include "support/spec_fixtures.h"
#include "support/yandex_sources.h"
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
#include <QString>
#include <QStringList>
#include <QTest>

#include <algorithm>
#include <optional>

// YandexSource::Catalog against the spec/player scenarios, on the spec API fixtures. Each test names
// the scenarios it checks.

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

Core::Track HeadTrack(const QString& id) {
    Yandex::Track track;
    track.id = id;
    track.albumId = QStringLiteral("1");
    track.title = QStringLiteral("T") + id;
    track.durationMs = 3'000;
    return YandexSource::TrackOf(track);
}

Core::Track KeptTrack() {
    Yandex::Track track;
    track.id = QStringLiteral("1");
    track.title = QStringLiteral("Kept");
    return YandexSource::TrackOf(track);
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
        Tests::YandexSources ports;
        Core::Player player;
        YandexSource::Catalog sources;
        QList<Core::SourceNotice> notices;
        explicit Stack(Yandex::Library* library)
            : ports(library)
            , player(&engine, &ports.registry)
            , sources(&player, library) {
            QObject::connect(&sources, &YandexSource::Catalog::noticed, [this](const Core::SourceNotice& notice) {
                notices << notice;
            });
        }
        bool saw(Core::SourceNoticeCode code, const QString& subject = {}, int count = 0) const {
            return std::any_of(notices.cbegin(), notices.cend(), [&](const Core::SourceNotice& notice) {
                return notice.code == code && (subject.isEmpty() || notice.subject == subject) && notice.count == count;
            });
        }
    };

    struct Found {
        Core::SearchResults results;
        QString error;
    };

    static Found find(YandexSource::Catalog& catalog, const QString& text) {
        Found found;
        bool done = false;
        catalog.find(text, [&](const Core::SearchResults& results, const QString& error) {
            found = Found{results, error};
            done = true;
        });
        QTest::qWaitFor([&] { return done; }, 3000);
        return found;
    }

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
        const QString sessionId = Tests::ExpectedObject("rotor-session-new/ok").value("sessionId").toString();
        server.fixture(
            "POST", QStringLiteral("/rotor/session/%1/feedback").arg(sessionId), "rotor-session-feedback/ok"
        );
        bool done = false;
        library.connectAccount([&](const Yandex::Account&, const QString&) { done = true; });
        QVERIFY(QTest::qWaitFor([&] { return done; }, 5000));
        QVERIFY(library.isLoggedIn());
    }

    void cleanup() { QTRY_COMPARE(api.pendingPosts(), 0); }

    void lastPickedSourceWins() {  // SRC-01
        Stack stack(&library);
        server.fixture("GET", "/users/42/likes/tracks", "users-likes-tracks/string-ids", 300);
        server.fixture("POST", "/tracks/", "tracks/two-tracks");
        server.fixture("POST", "/rotor/session/new", "rotor-session-new/ok");
        stack.sources.playLikes(false);
        stack.sources.playMyWave();
        QVERIFY(QTest::qWaitFor([&] { return stack.player.queueTitle() == QStringLiteral("My Vibe"); }, 3000));
        QTest::qWait(600);  // the likes response arrives now
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("My Vibe"));
        QCOMPARE(stack.player.entries().first().track.title, ExpectedTitle("rotor-session-new/ok"));
        QVERIFY(!stack.saw(Core::SourceNoticeCode::Loaded, QStringLiteral("Liked"), 2));
    }

    void staleFailureIsSilent() {  // SRC-02
        Stack stack(&library);
        server.fixture("GET", "/users/42/likes/tracks", "users-likes-artists/401-session-expired", 300);
        server.fixture("POST", "/rotor/session/new", "rotor-session-new/ok");
        stack.sources.playLikes(true);
        stack.sources.playMyWave();
        QVERIFY(QTest::qWaitFor([&] { return stack.player.queueTitle() == QStringLiteral("My Vibe"); }, 3000));
        QTest::qWait(600);
        QVERIFY(!stack.saw(Core::SourceNoticeCode::Failed));
    }

    void anArtistIsOnePickAcrossItsRequests() {  // SRC-03
        Stack stack(&library);
        server.fixture("GET", "/artists/9/track-ids-by-rating", "artists-track-ids-by-rating/ok", 400);
        server.fixture("POST", "/tracks/", "tracks/two-tracks");
        server.fixture("POST", "/rotor/session/new", "rotor-session-new/ok");
        stack.sources.playPick(Core::SearchPick{
            .kind = Core::SearchPick::Kind::Artist, .id = QStringLiteral("9"), .name = QStringLiteral("Кино")
        });
        stack.sources.playMyWave();
        QVERIFY(QTest::qWaitFor([&] { return stack.player.queueTitle() == QStringLiteral("My Vibe"); }, 3000));
        QTest::qWait(800);  // the artist's tracks arrive now
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("My Vibe"));
    }

    void failedSourceKeepsTheQueue() {  // SRC-04
        Stack stack(&library);
        stack.player.setSource(Core::QueueSource{.title = QStringLiteral("Kept"), .head = {KeptTrack()}}, false);
        server.fixture("GET", "/albums/4053/with-tracks", "account-status/500-empty");
        stack.sources.playAlbum(QStringLiteral("4053"), QStringLiteral("Звезда"));
        QVERIFY(
            QTest::qWaitFor([&] { return stack.saw(Core::SourceNoticeCode::Failed, QStringLiteral("Звезда")); }, 3000)
        );
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("Kept"));
    }

    void likesReplaceTheQueueAndCountTheTracks() {  // SRC-06
        Stack stack(&library);
        server.fixture("GET", "/users/42/likes/tracks", "users-likes-tracks/string-ids");
        server.fixture("POST", "/tracks/", "tracks/two-tracks");
        stack.sources.playLikes(false);
        QVERIFY(
            QTest::qWaitFor([&] { return stack.saw(Core::SourceNoticeCode::Loaded, QStringLiteral("Liked"), 2); }, 3000)
        );
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("Liked"));
        QCOMPARE(stack.player.entries().size(), 2);
        QCOMPARE(stack.player.entries().first().track.title, ExpectedTitle("tracks/two-tracks"));
    }

    void emptySourceKeepsTheQueue_data() {  // SRC-07, SRC-08
        QTest::addColumn<QString>("likes");
        QTest::addColumn<QString>("tracks");
        QTest::newRow("no likes") << QStringLiteral("users-likes-tracks/empty") << QStringLiteral("tracks/two-tracks");
        QTest::newRow("all unavailable") << QStringLiteral("users-likes-tracks/string-ids"
        ) << QStringLiteral("tracks/all-unavailable");
    }
    void emptySourceKeepsTheQueue() {
        QFETCH(QString, likes);
        QFETCH(QString, tracks);
        Stack stack(&library);
        stack.player.setSource(Core::QueueSource{.title = QStringLiteral("Kept"), .head = {KeptTrack()}}, false);
        server.fixture("GET", "/users/42/likes/tracks", likes);
        server.fixture("POST", "/tracks/", tracks);
        stack.sources.playLikes(true);
        QVERIFY(QTest::qWaitFor([&] { return stack.saw(Core::SourceNoticeCode::Empty, QStringLiteral("Liked")); }, 3000)
        );
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("Kept"));

        server.fixture("GET", "/artists/9/track-ids-by-rating", "artists-track-ids-by-rating/ok");
        stack.sources.playArtist(QStringLiteral("9"), QStringLiteral("Кино"));
        QVERIFY(QTest::qWaitFor(
            [&] {
                return stack.saw(Core::SourceNoticeCode::Empty, QStringLiteral("Кино"))
                    || stack.player.queueTitle() == QStringLiteral("Кино");
            },
            3000
        ));
        QCOMPARE(
            stack.player.queueTitle(),
            tracks == QStringLiteral("tracks/all-unavailable") ? QStringLiteral("Kept") : QStringLiteral("Кино")
        );
    }

    void waveWithoutPlayableTracksKeepsTheQueue() {  // WAVE-03
        Stack stack(&library);
        stack.player.setSource(Core::QueueSource{.title = QStringLiteral("Kept"), .head = {KeptTrack()}}, false);
        server.fixture("POST", "/rotor/session/new", "rotor-session-new/all-unavailable");
        const auto before = server.requests().size();
        stack.sources.playMyWave();
        QVERIFY(
            QTest::qWaitFor([&] { return stack.saw(Core::SourceNoticeCode::Empty, QStringLiteral("My Vibe")); }, 3000)
        );
        QTest::qWait(100);
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("Kept"));
        QVERIFY(feedbackEvents(before).isEmpty());  // no radioStarted for a wave that never starts
    }

    void aSearchsArtistOrAlbumQueuesItsTracks_data() {  // SRC-09, SRC-10
        QTest::addColumn<int>("kind");
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("title");
        QTest::addColumn<int>("count");
        QTest::newRow("artist"
        ) << static_cast<int>(Core::SearchPick::Kind::Artist)
          << QStringLiteral("9") << QStringLiteral("Кино") << 2;
        QTest::newRow("album"
        ) << static_cast<int>(Core::SearchPick::Kind::Album)
          << QStringLiteral("4053") << QStringLiteral("Звезда по имени Солнце") << 3;
    }
    void aSearchsArtistOrAlbumQueuesItsTracks() {
        QFETCH(int, kind);
        QFETCH(QString, id);
        QFETCH(QString, title);
        QFETCH(int, count);
        Stack stack(&library);
        server.fixture("GET", "/artists/9/track-ids-by-rating", "artists-track-ids-by-rating/ok");
        server.fixture("POST", "/tracks/", "tracks/two-tracks");
        server.fixture("GET", "/albums/4053/with-tracks", "albums-with-tracks/two-volumes");
        stack.sources.playPick(
            Core::SearchPick{.kind = static_cast<Core::SearchPick::Kind>(kind), .id = id, .name = title}
        );
        QVERIFY(QTest::qWaitFor([&] { return stack.player.queueTitle() == title; }, 3000));
        QCOMPARE(stack.player.entries().size(), count);
    }

    void aSearchShowsItsPartsAndTheBestOnce() {  // SRC-13
        Stack stack(&library);
        stack.player.setSource(Core::QueueSource{.title = QStringLiteral("Kept"), .head = {KeptTrack()}}, false);
        server.fixture("GET", "/search", "search/all-parts");
        const Found found = find(stack.sources, QStringLiteral("кино"));
        QVERIFY(found.error.isEmpty());
        QCOMPARE(found.results.bestArtist->id, QStringLiteral("41075"));
        QCOMPARE(found.results.bestArtist->name, QStringLiteral("КИНО"));
        QCOMPARE(found.results.artists.size(), 2);
        QCOMPARE(found.results.artists.front().name, QStringLiteral("Кинотеатр"));
        QCOMPARE(found.results.tracks.size(), 3);
        QCOMPARE(found.results.albums.size(), 3);
        QCOMPARE(found.results.albums.front().artists, QStringList{"MACAN"});
        QCOMPARE(found.results.albums.front().year, 2020);
        QVERIFY(found.results.albums.front().single);
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("Kept"));
        QVERIFY(stack.notices.isEmpty());
    }

    void aBestTrackIsShownOnceAndPlaysFirst() {  // SRC-13, SRC-18
        Stack stack(&library);
        server.fixture("GET", "/search", "search/best-track");
        const Found found = find(stack.sources, QStringLiteral("кино"));
        QCOMPARE(found.results.bestTrack->title, QStringLiteral("Кукушка"));
        QVERIFY(found.results.tracks.isEmpty());
        stack.sources.playTracks(QStringLiteral("Search: кино"), found.results.playable(), 0, std::nullopt);
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("Search: кино"));
        QCOMPARE(stack.player.entries().size(), 1);
        QVERIFY(stack.saw(Core::SourceNoticeCode::Loaded, QStringLiteral("Search: кино"), 1));
    }

    void aFailedSearchKeepsTheQueueAndSaysNothing() {  // SRC-15
        Stack stack(&library);
        stack.player.setSource(Core::QueueSource{.title = QStringLiteral("Kept"), .head = {KeptTrack()}}, false);
        server.fixture("GET", "/search", "users-likes-artists/401-session-expired");
        const Found found = find(stack.sources, QStringLiteral("кино"));
        QVERIFY(!found.error.isEmpty());
        QVERIFY(found.results.isEmpty());
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("Kept"));
        QVERIFY(stack.notices.isEmpty());
    }

    void anOpenedAlbumPlaysFromTheChosenTrack() {  // SRC-16
        Stack stack(&library);
        server.fixture("GET", "/albums/4053/with-tracks", "albums-with-tracks/two-volumes");
        const Core::SearchPick album{
            .kind = Core::SearchPick::Kind::Album, .id = QStringLiteral("4053"), .name = QStringLiteral("Звезда")
        };
        QList<Core::Track> tracks;
        bool opened = false;
        stack.sources.open(album, [&](const QList<Core::Track>& found, const QString& error) {
            QVERIFY(error.isEmpty());
            tracks = found;
            opened = true;
        });
        QVERIFY(QTest::qWaitFor([&] { return opened; }, 3000));
        QCOMPARE(tracks.size(), 3);
        QVERIFY(stack.player.entries().isEmpty());
        stack.sources.playTracks(album.name, tracks, 2, album);
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("Звезда"));
        QCOMPARE(stack.player.currentIndex(), 2);
        QCOMPARE(stack.player.currentTrack()->title, ExpectedTitle("albums-with-tracks/two-volumes", 2));
    }

    void tracksAndAlbumsAreAddedNextOrToTheEnd() {  // SRC-17, ADD-01, ADD-03
        Stack stack(&library);
        stack.player.setSource(Core::QueueSource{.title = QStringLiteral("Kept"), .head = {KeptTrack()}}, false);
        stack.sources.add(QStringLiteral("Search: кино"), {HeadTrack(QStringLiteral("7"))}, Core::AddPlacement::Next);
        QCOMPARE(stack.player.entries().size(), 2);
        QCOMPARE(stack.player.entries()[1].part, Core::EntryPart::Item);
        QVERIFY(stack.saw(Core::SourceNoticeCode::AddedNext, QStringLiteral("T7"), 1));
        server.fixture("GET", "/albums/4053/with-tracks", "albums-with-tracks/two-volumes");
        const Core::SearchPick album{
            .kind = Core::SearchPick::Kind::Album, .id = QStringLiteral("4053"), .name = QStringLiteral("Звезда")
        };
        stack.sources.addAlbum(album, Core::AddPlacement::End);
        QVERIFY(QTest::qWaitFor([&] { return stack.player.entries().size() == 5; }, 3000));
        QCOMPARE(stack.player.entries()[2].part, Core::EntryPart::Head);
        QVERIFY(stack.saw(Core::SourceNoticeCode::AddedToEnd, QStringLiteral("Звезда"), 3));
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("Kept"));
    }

    void anEmptyQueueTakesTheAddedTracksAndAnUnavailableOneChangesNothing() {  // ADD-05
        Stack stack(&library);
        Core::Track unavailable = HeadTrack(QStringLiteral("8"));
        unavailable.available = false;
        stack.sources.add(QStringLiteral("Search: кино"), {unavailable}, Core::AddPlacement::Next);
        QVERIFY(stack.player.entries().isEmpty());
        QVERIFY(stack.notices.isEmpty());
        stack.sources.add(QStringLiteral("Search: кино"), {HeadTrack(QStringLiteral("7"))}, Core::AddPlacement::End);
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("Search: кино"));
        QCOMPARE(stack.player.currentIndex(), 0);
        QCOMPARE(stack.player.state(), Core::Player::State::Stopped);
        QVERIFY(stack.saw(Core::SourceNoticeCode::AddedToEnd, QStringLiteral("T7"), 1));
    }

    void anAlbumThatFailsToLoadAddsNothing() {  // SRC-17
        Stack stack(&library);
        stack.player.setSource(Core::QueueSource{.title = QStringLiteral("Kept"), .head = {KeptTrack()}}, false);
        server.fixture("GET", "/albums/9/with-tracks", "users-likes-artists/401-session-expired");
        const Core::SearchPick album{
            .kind = Core::SearchPick::Kind::Album, .id = QStringLiteral("9"), .name = QStringLiteral("Broken")
        };
        stack.sources.addAlbum(album, Core::AddPlacement::Next);
        QVERIFY(
            QTest::qWaitFor([&] { return stack.saw(Core::SourceNoticeCode::Failed, QStringLiteral("Broken")); }, 3000)
        );
        QCOMPARE(stack.player.entries().size(), 1);
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
        server.audioTracks({QStringLiteral("38634572"), QStringLiteral("38634573")}, file.readAll());
        server.fixture("POST", "/rotor/session/new", "rotor-session-new/ok");
        const QJsonObject first = Tests::ExpectedObject("rotor-session-new/ok");
        const QJsonObject more = Tests::ExpectedObject("rotor-session-tracks/ok");
        const QString session = first.value("sessionId").toString();
        server.fixture("POST", QStringLiteral("/rotor/session/%1/tracks").arg(session), "rotor-session-tracks/ok");
        server.fixture("POST", "/users/42/dislikes/tracks/add-multiple", "users-dislikes-tracks-add-multiple/ok");
        const auto before = server.requests().size();

        stack.sources.playMyWave();
        QVERIFY(QTest::qWaitFor([&] { return feedbackEvents(before).size() >= 2; }, 8000));
        const QJsonObject newBody = QJsonDocument::fromJson(server.last("/rotor/session/new")->body).object();
        QCOMPARE(newBody.value("seeds").toArray(), QJsonArray{"user:onyourwave"});  // WAVE-02
        QCOMPARE(stack.sources.lastWaveSeeds(), QStringList{"user:onyourwave"});
        // The unavailable track of the batch is left out (SRC-05).
        for (const Core::QueueEntry& entry : stack.player.entries()) {
            QVERIFY(entry.track.available);
        }

        const Core::TrackKey firstKey = stack.player.entries().first().track.key;
        const std::optional<Yandex::Track> firstQueued =
            YandexSource::YandexTrackOf(stack.player.entries().first().track);
        QVERIFY(firstQueued);
        const QString firstId = firstQueued->id;
        const QString firstTrack = firstId + u':' + firstQueued->albumId;
        QList<QJsonObject> events = feedbackEvents(before);
        QCOMPARE(events[0].value("type").toString(), QStringLiteral("radioStarted"));  // TRK-03
        QCOMPARE(events[0].value("batchId").toString(), first.value("batchId").toString());
        QCOMPARE(events[1].value("type").toString(), QStringLiteral("trackStarted"));  // TRK-04
        QCOMPARE(events[1].value("trackId").toString(), firstTrack);
        QCOMPARE(events[1].value("batchId").toString(), first.value("batchId").toString());

        // One track left: loading more was asked for with the queue's last ids (WAVE-05).
        QVERIFY(
            QTest::qWaitFor([&] { return server.last(QStringLiteral("/rotor/session/%1/tracks").arg(session)); }, 3000)
        );
        const QByteArray moreRequest = server.last(QStringLiteral("/rotor/session/%1/tracks").arg(session))->body;
        const QJsonObject moreBody = QJsonDocument::fromJson(moreRequest).object();
        QCOMPARE(moreBody.value("queue").toArray(), QJsonArray{firstId});

        // Dislike: the request, then a skip and the next track, from the new batch (TRK-07).
        QVERIFY(QTest::qWaitFor([&] { return stack.player.entries().size() == 2; }, 3000));
        stack.sources.dislikeAndSkip(firstKey);
        QVERIFY(QTest::qWaitFor([&] { return feedbackEvents(before).size() >= 4; }, 5000));
        QCOMPARE(server.last("/users/42/dislikes/tracks/add-multiple")->formValue("track-ids"), firstId);
        events = feedbackEvents(before);
        QCOMPARE(events[2].value("type").toString(), QStringLiteral("skip"));  // TRK-06
        QCOMPARE(events[2].value("trackId").toString(), firstTrack);
        QCOMPARE(events[3].value("type").toString(), QStringLiteral("trackStarted"));
        QCOMPARE(events[3].value("batchId").toString(), more.value("batchId").toString());
        QCOMPARE(stack.player.currentIndex(), 1);
        stack.player.stop();
    }

    void theHistoryVibeFollowsTheHeadFromTheTracksThatStarted() {  // CONT-04, CONT-05, CONT-08
        QFile file(QStringLiteral(QIYAA_TEST_DATA "/sine440_3s.mp3"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        Stack stack(&library);
        if (!stack.engine.init().ok) {
            QSKIP("no audio output");
        }
        stack.engine.setVolume(0);
        stack.player.setHistoryWaveFactory(YandexSource::HistoryWaves(&library));
        const QJsonObject first = Tests::ExpectedObject("rotor-session-new/ok");
        const QString firstWaveTrack = first.value("tracks").toArray().at(0).toObject().value("id").toString();
        server.audioTracks({QStringLiteral("101"), QStringLiteral("102"), firstWaveTrack}, file.readAll());
        server.fixture("POST", "/rotor/session/new", "rotor-session-new/ok");
        const auto before = server.requests().size();

        stack.player.setSource(
            Core::QueueSource{.title = QStringLiteral("Head"), .head = {HeadTrack("101"), HeadTrack("102")}}, false
        );
        stack.player.playIndex(1);
        QVERIFY(QTest::qWaitFor([&] { return stack.player.entries().size() > 2; }, 8000));
        const QJsonObject newBody = QJsonDocument::fromJson(server.last("/rotor/session/new")->body).object();
        QCOMPARE(newBody.value("seeds").toArray(), QJsonArray{"track:102"});
        for (const Core::QueueEntry& entry : stack.player.entries()) {
            QVERIFY(entry.track.available);
        }
        QCOMPARE(stack.player.entries()[2].part, Core::EntryPart::Continuation);
        QCOMPARE(YandexSource::CatalogId(stack.player.entries()[2].track.key), firstWaveTrack);

        stack.player.next();
        QVERIFY(QTest::qWaitFor([&] { return feedbackEvents(before).size() >= 2; }, 8000));
        const QList<QJsonObject> events = feedbackEvents(before);
        QCOMPARE(events[0].value("type").toString(), QStringLiteral("radioStarted"));  // TRK-03
        QCOMPARE(events[0].value("batchId").toString(), first.value("batchId").toString());
        QCOMPARE(events[1].value("type").toString(), QStringLiteral("trackStarted"));
        QVERIFY(events[1].value("trackId").toString().startsWith(firstWaveTrack));
        stack.player.stop();
    }

    void aHistoryVibeThatFailsToStartSaysSo() {  // CONT-07
        QFile file(QStringLiteral(QIYAA_TEST_DATA "/sine440_3s.mp3"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        Stack stack(&library);
        if (!stack.engine.init().ok) {
            QSKIP("no audio output");
        }
        stack.engine.setVolume(0);
        stack.player.setHistoryWaveFactory(YandexSource::HistoryWaves(&library));
        server.audioTracks({QStringLiteral("101")}, file.readAll());
        server.json("POST", "/rotor/session/new", "{}", 500);
        QStringList failures;
        connect(&stack.player, &Core::Player::noticed, this, [&](const Core::PlayerNotice& notice) {
            if (notice.code == Core::NoticeCode::VibeFailed) {
                failures << notice.detail;
            }
        });
        stack.player.setSource(Core::QueueSource{.title = QStringLiteral("Head"), .head = {HeadTrack("101")}}, true);
        QVERIFY(QTest::qWaitFor([&] { return !failures.isEmpty(); }, 8000));
        QVERIFY(!failures.first().isEmpty());
        QCOMPARE(stack.player.entries().size(), 1);
        stack.player.stop();
        server.fixture("POST", "/rotor/session/new", "rotor-session-new/ok");
    }
};

QTEST_GUILESS_MAIN(TestSources)
#include "sources_test.moc"
