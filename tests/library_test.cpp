#include "audio/audio_engine.h"
#include "core/player.h"
#include "support/mock_http_server.h"
#include "ui/library_menu.h"
#include "yandex/api_client.h"
#include "yandex/library.h"
#include "yandex/oauth.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QUrl>

#include <functional>

namespace {

// JSON with single quotes (moc can't parse raw string literals).
QByteArray J(const char* singleQuoted) {
    return QByteArray(singleQuoted).replace('\'', '"');
}

QByteArray TrackJson(int id, const char* title, int albumId = 0) {
    QByteArray json = "{\"id\":" + QByteArray::number(id) + ",\"title\":\"" + title
        + "\",\"artists\":[{\"name\":\"Artist\"}],\"durationMs\":180000";
    if (albumId) {
        json += ",\"albums\":[{\"id\":" + QByteArray::number(albumId) + "}]";
    }
    return json + "}";
}

template <typename T>
struct Result {
    T value{};
    QString error;
    bool done = false;
    auto callback() {
        return [this](const T& receivedValue, const QString& receivedError) {
            value = receivedValue;
            error = receivedError;
            done = true;
        };
    }
    bool wait() {
        return QTest::qWaitFor([this] { return done; }, 5000);
    }
};

class LocalNetworkAccessManager : public QNetworkAccessManager {
protected:
    QNetworkReply*
    createRequest(Operation op, const QNetworkRequest& request, QIODevice* outgoingData) override {
        QNetworkRequest localRequest(request);
        QUrl url = localRequest.url();
        if (url.scheme() == QLatin1String("https") && url.host() == QLatin1String("127.0.0.1")) {
            url.setScheme(QStringLiteral("http"));
            localRequest.setUrl(url);
        }
        return QNetworkAccessManager::createRequest(op, localRequest, outgoingData);
    }
};

}  // namespace

class TestLibrary : public QObject {
    Q_OBJECT
private:
    Tests::MockHttpServer server;
    QNetworkAccessManager networkManager;
    Yandex::ApiClient api{&networkManager};
    Yandex::Library library{&api};

private Q_SLOTS:
    void initTestCase() {
        api.setBaseUrl(server.baseUrl());
        api.setToken(QStringLiteral("test-token"));
        server.result(
            "GET", "/account/status",
            J("{'account':{'uid':42,'login':'kick','displayName':'Kick'}}")
        );
    }

    void connectsAccountWithAuthHeader() {
        Result<Yandex::Account> account;
        library.connectAccount(account.callback());
        QVERIFY(account.wait());
        QVERIFY2(account.error.isEmpty(), qPrintable(account.error));
        QCOMPARE(account.value.uid, QStringLiteral("42"));
        QVERIFY(library.isLoggedIn());
        QCOMPARE(
            server.last("/account/status")->headers.value("authorization"),
            QByteArray("OAuth test-token")
        );
    }

    void likedTracksFetchesMetadataAndRemembersLikes() {
        server.result(
            "GET", "/users/42/likes/tracks",
            J("{'library':{'uid':42,'tracks':[{'id':'1','albumId':'10'},{'id':'2','albumId':'20'}]}"
              "}")
        );
        server.result(
            "POST", "/tracks/", "[" + TrackJson(1, "One", 10) + "," + TrackJson(2, "Two", 20) + "]"
        );
        Result<QList<Yandex::Track>> liked;
        library.likedTracks(liked.callback());
        QVERIFY(liked.wait());
        QCOMPARE(liked.value.size(), 2);
        QCOMPARE(liked.value[1].title, QStringLiteral("Two"));
        QCOMPARE(server.last("/tracks/")->formValue("track-ids"), QStringLiteral("1,2"));
        QVERIFY(library.isLiked("1"));
        QVERIFY(!library.isLiked("3"));
    }

    void userPlaylistsAndTheirEmbeddedTracksAreParsed() {
        server.result(
            "GET", "/users/42/playlists/list",
            J("[{'uid':42,'kind':1003,'title':'Дорога','trackCount':2}]")
        );
        Result<QList<Yandex::PlaylistReference>> lists;
        library.userPlaylists(lists.callback());
        QVERIFY(lists.wait());
        QCOMPARE(lists.value.size(), 1);
        QCOMPARE(lists.value[0].kind, QStringLiteral("1003"));
        QCOMPARE(lists.value[0].title, QStringLiteral("Дорога"));

        // Embedded track objects are used directly.
        server.result(
            "GET", "/users/42/playlists/1003",
            "{\"tracks\":[{\"id\":5,\"track\":" + TrackJson(5, "Five") + "}]}"
        );
        Result<QList<Yandex::Track>> tracks;
        library.playlistTracks(lists.value[0], tracks.callback());
        QVERIFY(tracks.wait());
        QCOMPARE(tracks.value.size(), 1);
        QCOMPARE(tracks.value[0].title, QStringLiteral("Five"));
    }

    void playlistWithoutEmbeddedTracksFetchesByIds() {
        server.result("GET", "/users/7/playlists/3", J("{'tracks':[{'id':1},{'id':2}]}"));
        Result<QList<Yandex::Track>> tracks;
        library.playlistTracks(Yandex::PlaylistReference{"7", "3", "x", 2}, tracks.callback());
        QVERIFY(tracks.wait());
        QCOMPARE(tracks.value.size(), 2);
        QCOMPARE(server.last("/tracks/")->formValue("track-ids"), QStringLiteral("1,2"));
    }

    void likedArtistsAndTheirTopTracksAreParsed() {
        server.result(
            "GET", "/users/42/likes/artists",
            J("[{'id':9,'name':'Кино'},{'artist':{'id':10,'name':'Земфира'}}]")
        );
        Result<QList<Yandex::NamedReference>> artists;
        library.likedArtists(artists.callback());
        QVERIFY(artists.wait());
        QCOMPARE(artists.value.size(), 2);
        QCOMPARE(artists.value[1].name, QStringLiteral("Земфира"));

        server.result(
            "GET", "/artists/9/track-ids-by-rating", J("{'artist':{},'tracks':['1','2']}")
        );
        Result<QList<Yandex::Track>> top;
        library.artistTopTracks("9", top.callback());
        QVERIFY(top.wait());
        QCOMPARE(top.value.size(), 2);
    }

    void albumsSkipPodcasts() {
        server.result("GET", "/users/42/likes/albums", J("[{'id':100},{'id':200}]"));
        server.result(
            "POST", "/albums",
            J("[{'id':100,'title':'Звезда','artists':[{'name':'Кино'}]},{'id':200,'title':'Pod','"
              "type':'podcast'}]")
        );
        Result<QList<Yandex::NamedReference>> albums;
        library.likedAlbums(albums.callback());
        QVERIFY(albums.wait());
        QCOMPARE(albums.value.size(), 1);
        QCOMPARE(albums.value[0].name, QStringLiteral("Кино - Звезда"));
        QCOMPARE(server.last("/albums")->formValue("album-ids"), QStringLiteral("100,200"));

        server.result(
            "GET", "/albums/100/with-tracks",
            "{\"id\":100,\"volumes\":[[" + TrackJson(1, "A") + "],[" + TrackJson(2, "B") + "]]}"
        );
        Result<QList<Yandex::Track>> tracks;
        library.albumTracks("100", tracks.callback());
        QVERIFY(tracks.wait());
        QCOMPARE(tracks.value.size(), 2);
        QCOMPARE(tracks.value[0].albumId, QStringLiteral("100"));
    }

    void stationsAreListedWithTypeTagIds() {
        server.result(
            "GET", "/rotor/stations/list",
            J("[{'station':{'id':{'type':'genre','tag':'rock'},'name':'Рок'}}]")
        );
        Result<QList<Yandex::Station>> stationList;
        library.stations(stationList.callback());
        QVERIFY(stationList.wait());
        QCOMPARE(stationList.value.size(), 1);
        QCOMPARE(stationList.value[0].id, QStringLiteral("genre:rock"));
        QCOMPARE(
            server.last("/rotor/stations/list")->query.queryItemValue("language"),
            QStringLiteral("ru")
        );
    }

    void waveSessionStartsFromSeedsAndContinuesWithTheQueue() {
        server.result(
            "POST", "/rotor/session/new",
            "{\"radioSessionId\":\"S1\",\"batchId\":\"B1\",\"sequence\":[{\"type\":\"track\","
            "\"track\":"
                + TrackJson(1, "W1") + "}]}"
        );
        Result<Yandex::WaveBatch> first;
        library.startWave({"user:onyourwave"}, first.callback());
        QVERIFY(first.wait());
        QCOMPARE(first.value.sessionId, QStringLiteral("S1"));
        QCOMPARE(first.value.tracks.size(), 1);
        const QJsonObject body =
            QJsonDocument::fromJson(server.last("/rotor/session/new")->body).object();
        QCOMPARE(
            body.value("seeds").toArray().first().toString(), QStringLiteral("user:onyourwave")
        );
        QVERIFY(body.value("includeTracksInResponse").toBool());

        server.result(
            "POST", "/rotor/session/S1/tracks",
            "{\"batchId\":\"B2\",\"sequence\":[{\"track\":" + TrackJson(2, "W2") + "}]}"
        );
        Result<Yandex::WaveBatch> more;
        library.moreWave("S1", {"1"}, more.callback());
        QVERIFY(more.wait());
        QCOMPARE(more.value.tracks.value(0).title, QStringLiteral("W2"));
        QCOMPARE(more.value.sessionId, QStringLiteral("S1"));
        const QJsonObject moreBody =
            QJsonDocument::fromJson(server.last("/rotor/session/S1/tracks")->body).object();
        QCOMPARE(moreBody.value("queue").toArray().first().toString(), QStringLiteral("1"));
    }

    void searchReportsTheBestArtist() {
        server.result(
            "GET", "/search",
            "{\"best\":{\"type\":\"artist\",\"result\":{\"id\":9,\"name\":\"Кино\"}},"
            "\"tracks\":{\"results\":["
                + TrackJson(3, "Кукушка") + "]}}"
        );
        Result<Yandex::SearchResult> found;
        library.search("кино", found.callback());
        QVERIFY(found.wait());
        QCOMPARE(found.value.bestKind, Yandex::SearchResult::Kind::Artist);
        QCOMPARE(found.value.bestId, QStringLiteral("9"));
        QCOMPARE(found.value.bestName, QStringLiteral("Кино"));
        QCOMPARE(found.value.tracks.size(), 1);
        QCOMPARE(server.last("/search")->query.queryItemValue("text"), QStringLiteral("кино"));
    }

    void likeUnlikeAndDislikeUpdateTheLikes() {
        server.result("POST", "/users/42/likes/tracks/add-multiple", J("{'revision':1}"));
        server.result("POST", "/users/42/likes/tracks/remove", J("{'revision':2}"));
        server.result("POST", "/users/42/dislikes/tracks/add-multiple", J("{'revision':3}"));
        Result<bool> like;
        library.setLiked("77", true, like.callback());
        QVERIFY(like.wait());
        QVERIFY(library.isLiked("77"));
        QCOMPARE(
            server.last("/users/42/likes/tracks/add-multiple")->formValue("track-ids"),
            QStringLiteral("77")
        );
        Result<bool> unlike;
        library.setLiked("77", false, unlike.callback());
        QVERIFY(unlike.wait());
        QVERIFY(!library.isLiked("77"));
        Result<bool> dislike;
        library.dislike("1", dislike.callback());
        QVERIFY(dislike.wait());
        QVERIFY(dislike.value);
        QVERIFY(!library.isLiked("1"));
    }

    void errorsAreReported() {
        server.json(
            "GET", "/users/42/likes/artists",
            J("{'error':{'name':'session-expired','message':'Token expired'}}"), 401
        );
        Result<QList<Yandex::NamedReference>> artists;
        library.likedArtists(artists.callback());
        QVERIFY(artists.wait());
        QVERIFY(artists.error.contains("401"));
        QVERIFY(artists.error.contains("Token expired"));
    }

    void deviceLoginPollsUntilToken() {
        server.json(
            "POST", "/device/code",
            J("{'device_code':'DEV','user_code':'ABCD1234','verification_url':'https://ya.ru/"
              "device','interval':1,'expires_in':300}")
        );
        int polls = 0;
        server.on("POST", "/token", [&polls](const Tests::MockRequest& request) {
            if (request.formValue("code") != "DEV") {
                return Tests::MockResponse{400, J("{'error':'bad_verification_code'}")};
            }
            if (++polls < 2) {
                return Tests::MockResponse{400, J("{'error':'authorization_pending'}")};
            }
            return Tests::MockResponse{
                200, J("{'access_token':'NEW_TOKEN','token_type':'bearer'}")
            };
        });
        Yandex::DeviceLogin login(&networkManager);
        login.setBaseUrl(server.baseUrl());
        QSignalSpy codeReady(&login, &Yandex::DeviceLogin::codeReady);
        QSignalSpy succeeded(&login, &Yandex::DeviceLogin::succeeded);
        login.start();
        QVERIFY(codeReady.wait(3000));
        QCOMPARE(codeReady.first().at(0).toString(), QStringLiteral("ABCD1234"));
        QVERIFY(succeeded.wait(5000));
        QCOMPARE(succeeded.first().at(0).toString(), QStringLiteral("NEW_TOKEN"));
        QCOMPARE(polls, 2);
        QCOMPARE(server.last("/token")->formValue("grant_type"), QStringLiteral("device_code"));
    }

    void deviceLoginFailureIsReported() {
        server.json(
            "POST", "/device/code",
            J("{'error':'invalid_client','error_description':'Client not found'}"), 400
        );
        Yandex::DeviceLogin login(&networkManager);
        login.setBaseUrl(server.baseUrl());
        QSignalSpy failed(&login, &Yandex::DeviceLogin::failed);
        login.start();
        QVERIFY(failed.wait(3000));
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("Client not found"));
    }

    void slowLoadDoesNotReplaceNewerChoice() {
        Audio::AudioEngine engine;
        Core::Player player(&library, &engine);
        server.on("GET", "/users/42/likes/tracks", [](const Tests::MockRequest&) {
            return Tests::MockResponse{
                200, J("{'result':{'library':{'tracks':[{'id':'1'}]}}}"), 300
            };
        });
        server.result("POST", "/tracks/", "[" + TrackJson(1, "Liked") + "]");
        server.result(
            "POST", "/rotor/session/new",
            "{\"radioSessionId\":\"S9\",\"sequence\":[{\"track\":" + TrackJson(7, "Wave") + "}]}"
        );
        Ui::PlayLikes(&player, false);
        Ui::PlayMyWave(&player);
        QVERIFY(QTest::qWaitFor(
            [&] { return player.queueTitle() == QStringLiteral("Моя волна"); }, 3000
        ));
        QTest::qWait(600);  // the likes response arrives now
        QCOMPARE(player.queueTitle(), QStringLiteral("Моя волна"));
        QCOMPARE(player.playlist().first().title, QStringLiteral("Wave"));
    }

    void personalPlaylistsAndTheirRecommendationsAreParsed() {
        server.result(
            "GET", "/landing3",
            J("{'blocks':[{'type':'personal-playlists','entities':["
              "{'type':'personal-playlist','data':{'type':'playlistOfTheDay','data':{'uid':"
              "503646255,'kind':123,'title':'Плейлист дня','trackCount':60}}},"
              "{'type':'personal-playlist','data':{'data':{'owner':{'uid':503646255},'kind':456,'"
              "title':'Дежавю'}}}]}]}")
        );
        Result<QList<Yandex::PlaylistReference>> playlists;
        library.personalPlaylists(playlists.callback());
        QVERIFY(playlists.wait());
        QVERIFY2(playlists.error.isEmpty(), qPrintable(playlists.error));
        QCOMPARE(playlists.value.size(), 2);
        QCOMPARE(playlists.value[0].title, QStringLiteral("Плейлист дня"));
        QCOMPARE(playlists.value[1].ownerUid, QStringLiteral("503646255"));
        QCOMPARE(
            server.last("/landing3")->query.queryItemValue("blocks"),
            QStringLiteral("personalplaylists")
        );

        server.result(
            "GET", "/users/503646255/playlists/123/recommendations",
            "{\"batchId\":\"b\",\"tracks\":[" + TrackJson(8, "Rec", 80) + "]}"
        );
        Result<QList<Yandex::Track>> recommendations;
        library.playlistRecommendations(playlists.value[0], recommendations.callback());
        QVERIFY(recommendations.wait());
        QCOMPARE(recommendations.value.size(), 1);
        QCOMPARE(recommendations.value[0].title, QStringLiteral("Rec"));
    }

    void wheelOfWavesReadsAnUnwrappedBodyAndSkipsOtherItems() {
        server.json(
            "POST", "/wheel/new",
            J("{'wheelId':'w1','items':[{'type':'WAVE','id':'1','data':{'wave':{'name':'Бодрое','"
              "description':'d','seeds':['mood:energetic']}}},"
              "{'type':'OTHER','id':'2','data':{}}]}")
        );
        Result<QList<Yandex::Wave>> waves;
        library.wheelWaves({"user:onyourwave"}, waves.callback());
        QVERIFY(waves.wait());
        QVERIFY2(waves.error.isEmpty(), qPrintable(waves.error));
        QCOMPARE(waves.value.size(), 1);
        QCOMPARE(waves.value[0].seeds, QStringList{"mood:energetic"});
        const QJsonObject body = QJsonDocument::fromJson(server.last("/wheel/new")->body).object();
        QCOMPARE(body.value("context").toObject().value("type").toString(), QStringLiteral("WAVE"));
    }

    void waveFeedbackFallsBackToStationEndpoint() {
        Yandex::Track track =
            Yandex::ApiClient::ParseTrack(QJsonDocument::fromJson(TrackJson(5, "T", 50)).object());
        // Session endpoint works: only it is used.
        server.result("POST", "/rotor/session/OK1/feedback", "\"ok\"");
        library.waveFeedback(
            "OK1", "user:onyourwave", "B1", Yandex::WaveEvent::TrackStarted, &track
        );
        QVERIFY(QTest::qWaitFor(
            [&] { return server.last("/rotor/session/OK1/feedback") != nullptr; }, 3000
        ));
        const QJsonObject sessionBody =
            QJsonDocument::fromJson(server.last("/rotor/session/OK1/feedback")->body).object();
        QCOMPARE(sessionBody.value("batchId").toString(), QStringLiteral("B1"));
        QCOMPARE(
            sessionBody.value("event").toObject().value("type").toString(),
            QStringLiteral("trackStarted")
        );
        QCOMPARE(
            sessionBody.value("event").toObject().value("trackId").toString(),
            QStringLiteral("5:50")
        );

        // Session endpoint rejected: falls back to the station endpoint, and stays there.
        server.json(
            "POST", "/rotor/session/BAD/feedback", J("{'error':{'message':'not found'}}"), 404
        );
        server.result("POST", "/rotor/station/user:onyourwave/feedback", "\"ok\"");
        const auto before = server.requests().size();
        auto stationCalls = [&] {
            int calls = 0;
            for (qsizetype i = before; i < server.requests().size(); ++i) {
                calls += server.requests()[i].path == "/rotor/station/user:onyourwave/feedback";
            }
            return calls;
        };
        library.waveFeedback(
            "BAD", "user:onyourwave", "B2", Yandex::WaveEvent::Skip, &track, 12.34
        );
        QVERIFY(QTest::qWaitFor([&] { return stationCalls() == 1; }, 3000));
        const Tests::MockRequest* stationRequest =
            server.last("/rotor/station/user:onyourwave/feedback");
        QCOMPARE(stationRequest->query.queryItemValue("batch-id"), QStringLiteral("B2"));
        const QJsonObject stationBody = QJsonDocument::fromJson(stationRequest->body).object();
        QCOMPARE(stationBody.value("type").toString(), QStringLiteral("skip"));
        QCOMPARE(stationBody.value("totalPlayedSeconds").toDouble(), 12.3);
        library.waveFeedback(
            "BAD", "user:onyourwave", "B2", Yandex::WaveEvent::TrackStarted, &track
        );
        QVERIFY(QTest::qWaitFor([&] { return stationCalls() == 2; }, 3000));
        int sessionCalls = 0;
        for (qsizetype i = before; i < server.requests().size(); ++i) {
            sessionCalls += server.requests()[i].path == "/rotor/session/BAD/feedback";
        }
        QCOMPARE(sessionCalls, 1);
    }

    void waveFeedbackDoesNotResendAfterServerError() {
        Yandex::Track track =
            Yandex::ApiClient::ParseTrack(QJsonDocument::fromJson(TrackJson(5, "T", 50)).object());
        server.json("POST", "/rotor/session/S500/feedback", J("{'error':'oops'}"), 503);
        const auto before = server.requests().size();
        bool settled = false;
        const int pending = api.pendingPosts();
        auto connection =
            connect(&api, &Yandex::ApiClient::postsSettled, this, [&] { settled = true; });
        library.waveFeedback("S500", "user:onyourwave", "B1", Yandex::WaveEvent::Skip, &track, 3);
        QCOMPARE(api.pendingPosts(), pending + 1);
        QVERIFY(QTest::qWaitFor([&] { return settled; }, 3000));
        disconnect(connection);
        for (qsizetype i = before; i < server.requests().size(); ++i) {
            QVERIFY(!server.requests()[i].path.startsWith("/rotor/station/"));
        }
    }

    void postsSettleOnlyAfterTheFallback() {
        Yandex::Track track =
            Yandex::ApiClient::ParseTrack(QJsonDocument::fromJson(TrackJson(5, "T", 50)).object());
        server.json(
            "POST", "/rotor/session/S404/feedback", J("{'error':{'message':'not found'}}"), 404
        );
        server.result("POST", "/rotor/station/user:x/feedback", "\"ok\"");
        const auto before = server.requests().size();
        int stationCallsAtSettle = -1;
        auto stationCalls = [&] {
            int calls = 0;
            for (qsizetype i = before; i < server.requests().size(); ++i) {
                calls += server.requests()[i].path == "/rotor/station/user:x/feedback";
            }
            return calls;
        };
        auto connection = connect(&api, &Yandex::ApiClient::postsSettled, this, [&] {
            stationCallsAtSettle = stationCalls();
        });
        library.waveFeedback("S404", "user:x", "B1", Yandex::WaveEvent::Skip, &track, 3);
        QVERIFY(QTest::qWaitFor([&] { return stationCallsAtSettle >= 0; }, 3000));
        QCOMPARE(stationCallsAtSettle, 1);
        QCOMPARE(api.pendingPosts(), 0);
        disconnect(connection);
    }

    void playerReportsTrackEvents() {
        Audio::AudioEngine engine;
        Core::Player player(&library, &engine);
        server.result(
            "GET", "/tracks/1/download-info",
            "[{\"codec\":\"mp3\",\"bitrateInKbps\":320,\"downloadInfoUrl\":\""
                + server.baseUrl().toUtf8() + "/dl?x=1\"}]"
        );
        server.result(
            "GET", "/tracks/2/download-info",
            "[{\"codec\":\"mp3\",\"bitrateInKbps\":320,\"downloadInfoUrl\":\""
                + server.baseUrl().toUtf8() + "/dl?x=2\"}]"
        );
        server.json("GET", "/dl", J("{'host':'127.0.0.1:1','path':'/p','ts':'1','s':'s'}"));
        server.result("POST", "/play-audio", "\"ok\"");
        QList<Yandex::Track> tracks;
        for (int i = 1; i <= 2; ++i) {
            tracks << Yandex::ApiClient::ParseTrack(
                QJsonDocument::fromJson(TrackJson(i, "t")).object()
            );
        }
        QStringList log;
        player.setQueue(
            tracks, "W", false, {},
            [&](Core::Player::TrackEvent event, const Yandex::Track& track, double) {
                log << QStringLiteral("%1:%2").arg(int(event)).arg(track.id);
            }
        );
        player.playIndex(0);
        QVERIFY(QTest::qWaitFor([&] { return log.contains("0:1"); }, 3000));
        player.playIndex(1);
        QVERIFY(QTest::qWaitFor([&] { return log.contains("0:2"); }, 3000));
        QCOMPARE(log, (QStringList{"0:1", "2:1", "0:2"}));
        player.setQueue({}, "", false);
        QCOMPARE(log.last(), QStringLiteral("2:2"));
    }

private:
    struct PlaybackStack {
        LocalNetworkAccessManager networkManager;
        Yandex::ApiClient api{&networkManager};
        Yandex::Library library{&api};
        Audio::AudioEngine engine;
        Core::Player player{&library, &engine};
    };
    bool setUpAudio(PlaybackStack& playback, const QList<int>& ids) {
        QFile file(QStringLiteral(QIYAA_TEST_DATA "/sine440_3s.mp3"));
        if (!file.open(QIODevice::ReadOnly) || !playback.engine.init().ok) {
            return false;
        }
        playback.engine.setVolume(0);
        playback.api.setBaseUrl(server.baseUrl());
        const QByteArray mp3 = file.readAll();
        for (int id : ids) {
            server.result(
                "GET", QStringLiteral("/tracks/%1/download-info").arg(id),
                "[{\"codec\":\"mp3\",\"bitrateInKbps\":320,\"downloadInfoUrl\":\""
                    + server.baseUrl().toUtf8() + "/dlinfo" + QByteArray::number(id) + "\"}]"
            );
            server.json(
                "GET", QStringLiteral("/dlinfo%1").arg(id),
                "{\"host\":\"" + QUrl(server.baseUrl()).authority().toUtf8() + "\",\"path\":\"/t"
                    + QByteArray::number(id) + "\",\"ts\":\"1\",\"s\":\"s\"}"
            );
        }
        server.onPrefix("GET", "/get-mp3/", [mp3](const Tests::MockRequest&) {
            return Tests::MockResponse{200, mp3};
        });
        server.result("POST", "/play-audio", "\"ok\"");
        return true;
    }
    int requestsTo(const QString& path) const {
        int count = 0;
        for (const Tests::MockRequest& request : server.requests()) {
            count += request.path == path;
        }
        return count;
    }
    static QList<Yandex::Track> NumberedTracks(const QList<int>& ids) {
        QList<Yandex::Track> tracks;
        for (int id : ids) {
            tracks << Yandex::ApiClient::ParseTrack(
                QJsonDocument::fromJson(TrackJson(id, "t")).object()
            );
        }
        return tracks;
    }

private Q_SLOTS:
    void playerPreloadsAndAdvancesSeamlessly() {
        PlaybackStack playback;
        if (!setUpAudio(playback, {11, 12, 13})) {
            QSKIP("no audio output");
        }
        QStringList log;
        playback.player.setQueue(
            NumberedTracks({11, 12, 13}), "A", false, {},
            [&](Core::Player::TrackEvent event, const Yandex::Track& track, double) {
                log << QStringLiteral("%1:%2").arg(int(event)).arg(track.id);
            }
        );
        QSignalSpy advanced(&playback.engine, &Audio::AudioEngine::trackAdvanced);
        QSignalSpy finished(&playback.engine, &Audio::AudioEngine::trackFinished);
        playback.player.playIndex(0);
        QVERIFY(QTest::qWaitFor([&] { return playback.player.preloadedIndex() == 1; }, 5000));
        QVERIFY(QTest::qWaitFor(
            [&] { return playback.engine.state() == Audio::AudioEngine::State::Playing; }, 3000
        ));
        QTest::qWait(300);  // let the preload download complete
        QVERIFY(playback.player.seekTo(2.4));
        QVERIFY(advanced.wait(4000));
        QCOMPARE(finished.count(), 0);
        QCOMPARE(playback.player.currentIndex(), 1);
        QCOMPARE(log, (QStringList{"0:11", "1:11", "0:12"}));
        QCOMPARE(requestsTo("/tracks/12/download-info"), 1);
        QVERIFY(playback.engine.positionSeconds() < 0.5);

        // "Next" takes the preloaded track too.
        QVERIFY(QTest::qWaitFor([&] { return playback.player.preloadedIndex() == 2; }, 5000));
        playback.player.next();
        QCOMPARE(playback.player.currentIndex(), 2);
        QCOMPARE(log.mid(3), (QStringList{"2:12", "0:13"}));
        QCOMPARE(requestsTo("/tracks/13/download-info"), 1);
        playback.player.stop();
    }

    void playerRepreloadsWhenTheQueueChanges() {
        PlaybackStack playback;
        if (!setUpAudio(playback, {21, 22, 23})) {
            QSKIP("no audio output");
        }
        playback.player.setQueue(NumberedTracks({21, 22, 23}), "A", false);
        playback.player.playIndex(0);
        QVERIFY(QTest::qWaitFor([&] { return playback.player.preloadedIndex() == 1; }, 5000));
        playback.player.removeTracks({1});  // the preloaded track is gone: 23 follows now
        QVERIFY(QTest::qWaitFor(
            [&] {
                return playback.player.preloadedIndex() == 1
                    && requestsTo("/tracks/23/download-info") == 1;
            },
            5000
        ));
        QTest::qWait(300);
        QSignalSpy advanced(&playback.engine, &Audio::AudioEngine::trackAdvanced);
        QVERIFY(playback.player.seekTo(2.4));
        QVERIFY(advanced.wait(4000));
        QCOMPARE(playback.player.currentTrack()->id, QStringLiteral("23"));
        playback.player.stop();
    }

    void playerAsksEndlessSourceForMore() {
        Audio::AudioEngine engine;  // not initialised: nothing actually plays
        Core::Player player(&library, &engine);
        QList<Yandex::Track> batch;
        for (int i = 0; i < 3; ++i) {
            batch << Yandex::ApiClient::ParseTrack(
                QJsonDocument::fromJson(TrackJson(i + 1, "t")).object()
            );
        }
        int asked = 0;
        player.setQueue(
            batch, "Wave", false,
            [&](std::function<void(const QList<Yandex::Track>&)> done) {
                ++asked;
                done({Yandex::ApiClient::ParseTrack(
                    QJsonDocument::fromJson(TrackJson(99, "more")).object()
                )});
            }
        );
        QCOMPARE(player.playlist().size(), 3);
        player.playIndex(1);  // 2 tracks left -> ask for more
        QCOMPARE(asked, 1);
        QCOMPARE(player.playlist().size(), 4);
        QCOMPARE(player.playlist().last().title, QStringLiteral("more"));
    }
};

QTEST_GUILESS_MAIN(TestLibrary)
#include "library_test.moc"
