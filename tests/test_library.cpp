// Yandex Music layer against a local mock server: request shapes and parsing
// for every source the menu offers, likes, waves, search and device login.
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTest>

#include "MockHttpServer.h"
#include "core/Player.h"
#include "ui/LibraryMenu.h"
#include "yandex/Library.h"
#include "yandex/OAuth.h"

using namespace qiyaa;
using namespace qiyaa::yandex;

namespace {

// JSON with single quotes (moc can't parse raw string literals).
QByteArray J(const char* s) {
    return QByteArray(s).replace('\'', '"');
}

QByteArray trackJson(int id, const char* title, int album = 0) {
    QByteArray s = "{\"id\":" + QByteArray::number(id) + ",\"title\":\"" + title +
                   "\",\"artists\":[{\"name\":\"Artist\"}],\"durationMs\":180000";
    if (album) s += ",\"albums\":[{\"id\":" + QByteArray::number(album) + "}]";
    return s + "}";
}

// Waits for an async callback.
template <typename T>
struct Result {
    T value{};
    QString error;
    bool done = false;
    auto cb() {
        return [this](const T& v, const QString& e) {
            value = v;
            error = e;
            done = true;
        };
    }
    bool wait() { return QTest::qWaitFor([this] { return done; }, 5000); }
};

// Serves the "https" track links (host 127.0.0.1) from the plain-HTTP mock server.
class LocalNam : public QNetworkAccessManager {
protected:
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& request, QIODevice* data) override {
        QNetworkRequest r(request);
        QUrl u = r.url();
        if (u.scheme() == QLatin1String("https") && u.host() == QLatin1String("127.0.0.1")) {
            u.setScheme(QStringLiteral("http"));
            r.setUrl(u);
        }
        return QNetworkAccessManager::createRequest(op, r, data);
    }
};

}  // namespace

class TestLibrary : public QObject {
    Q_OBJECT
private:
    MockHttpServer server;
    QNetworkAccessManager nam;
    ApiClient api{&nam};
    Library lib{&api};

private Q_SLOTS:
    void initTestCase() {
        api.setBaseUrl(server.baseUrl());
        api.setToken(QStringLiteral("test-token"));
        server.result("GET", "/account/status",
                      J("{'account':{'uid':42,'login':'kick','displayName':'Kick'}}"));
    }

    void connectsAccountWithAuthHeader() {
        Result<Account> r;
        lib.connectAccount(r.cb());
        QVERIFY(r.wait());
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.value.uid, QStringLiteral("42"));
        QVERIFY(lib.isLoggedIn());
        QCOMPARE(server.last("/account/status")->headers.value("authorization"), QByteArray("OAuth test-token"));
    }

    void likedTracksFetchesMetadataAndRemembersLikes() {
        server.result("GET", "/users/42/likes/tracks",
                      J("{'library':{'uid':42,'tracks':[{'id':'1','albumId':'10'},{'id':'2','albumId':'20'}]}}"));
        server.result("POST", "/tracks/", "[" + trackJson(1, "One", 10) + "," + trackJson(2, "Two", 20) + "]");
        Result<QList<Track>> r;
        lib.likedTracks(r.cb());
        QVERIFY(r.wait());
        QCOMPARE(r.value.size(), 2);
        QCOMPARE(r.value[1].title, QStringLiteral("Two"));
        QCOMPARE(server.last("/tracks/")->formValue("track-ids"), QStringLiteral("1,2"));
        QVERIFY(lib.isLiked("1"));
        QVERIFY(!lib.isLiked("3"));
    }

    void playlistsAndTheirTracks() {
        server.result("GET", "/users/42/playlists/list",
                      J("[{'uid':42,'kind':1003,'title':'Дорога','trackCount':2}]"));
        Result<QList<PlaylistRef>> lists;
        lib.userPlaylists(lists.cb());
        QVERIFY(lists.wait());
        QCOMPARE(lists.value.size(), 1);
        QCOMPARE(lists.value[0].kind, QStringLiteral("1003"));
        QCOMPARE(lists.value[0].title, QStringLiteral("Дорога"));

        // Embedded track objects are used directly.
        server.result("GET", "/users/42/playlists/1003",
                      "{\"tracks\":[{\"id\":5,\"track\":" + trackJson(5, "Five") + "}]}");
        Result<QList<Track>> tracks;
        lib.playlistTracks(lists.value[0], tracks.cb());
        QVERIFY(tracks.wait());
        QCOMPARE(tracks.value.size(), 1);
        QCOMPARE(tracks.value[0].title, QStringLiteral("Five"));
    }

    void playlistWithoutEmbeddedTracksFetchesByIds() {
        server.result("GET", "/users/7/playlists/3", J("{'tracks':[{'id':1},{'id':2}]}"));
        Result<QList<Track>> tracks;
        lib.playlistTracks(PlaylistRef{"7", "3", "x", 2}, tracks.cb());
        QVERIFY(tracks.wait());
        QCOMPARE(tracks.value.size(), 2);
        QCOMPARE(server.last("/tracks/")->formValue("track-ids"), QStringLiteral("1,2"));
    }

    void artistsAndTopTracks() {
        server.result("GET", "/users/42/likes/artists",
                      J("[{'id':9,'name':'Кино'},{'artist':{'id':10,'name':'Земфира'}}]"));
        Result<QList<NamedRef>> artists;
        lib.likedArtists(artists.cb());
        QVERIFY(artists.wait());
        QCOMPARE(artists.value.size(), 2);
        QCOMPARE(artists.value[1].name, QStringLiteral("Земфира"));

        server.result("GET", "/artists/9/track-ids-by-rating", J("{'artist':{},'tracks':['1','2']}"));
        Result<QList<Track>> top;
        lib.artistTopTracks("9", top.cb());
        QVERIFY(top.wait());
        QCOMPARE(top.value.size(), 2);
    }

    void albumsSkipPodcasts() {
        server.result("GET", "/users/42/likes/albums", J("[{'id':100},{'id':200}]"));
        server.result("POST", "/albums",
                      J("[{'id':100,'title':'Звезда','artists':[{'name':'Кино'}]},{'id':200,'title':'Pod','type':'podcast'}]"));
        Result<QList<NamedRef>> albums;
        lib.likedAlbums(albums.cb());
        QVERIFY(albums.wait());
        QCOMPARE(albums.value.size(), 1);
        QCOMPARE(albums.value[0].name, QStringLiteral("Кино - Звезда"));
        QCOMPARE(server.last("/albums")->formValue("album-ids"), QStringLiteral("100,200"));

        server.result("GET", "/albums/100/with-tracks",
                      "{\"id\":100,\"volumes\":[[" + trackJson(1, "A") + "],[" + trackJson(2, "B") + "]]}");
        Result<QList<Track>> tracks;
        lib.albumTracks("100", tracks.cb());
        QVERIFY(tracks.wait());
        QCOMPARE(tracks.value.size(), 2);            // all volumes
        QCOMPARE(tracks.value[0].albumId, QStringLiteral("100"));
    }

    void stations() {
        server.result("GET", "/rotor/stations/list",
                      J("[{'station':{'id':{'type':'genre','tag':'rock'},'name':'Рок'}}]"));
        Result<QList<Station>> r;
        lib.stations(r.cb());
        QVERIFY(r.wait());
        QCOMPARE(r.value.size(), 1);
        QCOMPARE(r.value[0].id, QStringLiteral("genre:rock"));
        QCOMPARE(server.last("/rotor/stations/list")->query.queryItemValue("language"), QStringLiteral("ru"));
    }

    void waveSession() {
        server.result("POST", "/rotor/session/new",
                      "{\"radioSessionId\":\"S1\",\"batchId\":\"B1\",\"sequence\":[{\"type\":\"track\",\"track\":" +
                          trackJson(1, "W1") + "}]}");
        Result<WaveBatch> first;
        lib.startWave({"user:onyourwave"}, first.cb());
        QVERIFY(first.wait());
        QCOMPARE(first.value.sessionId, QStringLiteral("S1"));
        QCOMPARE(first.value.tracks.size(), 1);
        const QJsonObject body = QJsonDocument::fromJson(server.last("/rotor/session/new")->body).object();
        QCOMPARE(body.value("seeds").toArray().first().toString(), QStringLiteral("user:onyourwave"));
        QVERIFY(body.value("includeTracksInResponse").toBool());

        server.result("POST", "/rotor/session/S1/tracks",
                      "{\"batchId\":\"B2\",\"sequence\":[{\"track\":" + trackJson(2, "W2") + "}]}");
        Result<WaveBatch> more;
        lib.moreWave("S1", {"1"}, more.cb());
        QVERIFY(more.wait());
        QCOMPARE(more.value.tracks.value(0).title, QStringLiteral("W2"));
        QCOMPARE(more.value.sessionId, QStringLiteral("S1"));
        const QJsonObject moreBody = QJsonDocument::fromJson(server.last("/rotor/session/S1/tracks")->body).object();
        QCOMPARE(moreBody.value("queue").toArray().first().toString(), QStringLiteral("1"));
    }

    void searchBestArtist() {
        server.result("GET", "/search",
                      "{\"best\":{\"type\":\"artist\",\"result\":{\"id\":9,\"name\":\"Кино\"}},"
                      "\"tracks\":{\"results\":[" + trackJson(3, "Кукушка") + "]}}");
        Result<SearchResult> r;
        lib.search("кино", r.cb());
        QVERIFY(r.wait());
        QCOMPARE(r.value.bestType, QStringLiteral("artist"));
        QCOMPARE(r.value.bestId, QStringLiteral("9"));
        QCOMPARE(r.value.bestName, QStringLiteral("Кино"));
        QCOMPARE(r.value.tracks.size(), 1);
        QCOMPARE(server.last("/search")->query.queryItemValue("text"), QStringLiteral("кино"));
    }

    void likeUnlikeDislike() {
        server.result("POST", "/users/42/likes/tracks/add-multiple", J("{'revision':1}"));
        server.result("POST", "/users/42/likes/tracks/remove", J("{'revision':2}"));
        server.result("POST", "/users/42/dislikes/tracks/add-multiple", J("{'revision':3}"));
        Result<bool> like;
        lib.setLiked("77", true, like.cb());
        QVERIFY(like.wait());
        QVERIFY(lib.isLiked("77"));
        QCOMPARE(server.last("/users/42/likes/tracks/add-multiple")->formValue("track-ids"), QStringLiteral("77"));
        Result<bool> unlike;
        lib.setLiked("77", false, unlike.cb());
        QVERIFY(unlike.wait());
        QVERIFY(!lib.isLiked("77"));
        Result<bool> dislike;
        lib.dislike("1", dislike.cb());
        QVERIFY(dislike.wait());
        QVERIFY(dislike.value);
        QVERIFY(!lib.isLiked("1"));
    }

    void errorsAreReported() {
        server.json("GET", "/users/42/likes/artists", J("{'error':{'name':'session-expired','message':'Token expired'}}"), 401);
        Result<QList<NamedRef>> r;
        lib.likedArtists(r.cb());
        QVERIFY(r.wait());
        QVERIFY(r.error.contains("401"));
        QVERIFY(r.error.contains("Token expired"));
    }

    void deviceLoginPollsUntilToken() {
        server.json("POST", "/device/code",
                    J("{'device_code':'DEV','user_code':'ABCD1234','verification_url':'https://ya.ru/device','interval':1,'expires_in':300}"));
        int polls = 0;
        server.on("POST", "/token", [&polls](const MockRequest& req) {
            if (req.formValue("code") != "DEV") return MockResponse{400, J("{'error':'bad_verification_code'}")};
            if (++polls < 2) return MockResponse{400, J("{'error':'authorization_pending'}")};
            return MockResponse{200, J("{'access_token':'NEW_TOKEN','token_type':'bearer'}")};
        });
        DeviceLogin login(&nam);
        login.setBaseUrl(server.baseUrl());
        QSignalSpy code(&login, &DeviceLogin::codeReady);
        QSignalSpy ok(&login, &DeviceLogin::succeeded);
        login.start();
        QVERIFY(code.wait(3000));
        QCOMPARE(code.first().at(0).toString(), QStringLiteral("ABCD1234"));
        QVERIFY(ok.wait(5000));
        QCOMPARE(ok.first().at(0).toString(), QStringLiteral("NEW_TOKEN"));
        QCOMPARE(polls, 2);
        QCOMPARE(server.last("/token")->formValue("grant_type"), QStringLiteral("device_code"));
    }

    void deviceLoginFailureIsReported() {
        server.json("POST", "/device/code", J("{'error':'invalid_client','error_description':'Client not found'}"), 400);
        DeviceLogin login(&nam);
        login.setBaseUrl(server.baseUrl());
        QSignalSpy failed(&login, &DeviceLogin::failed);
        login.start();
        QVERIFY(failed.wait(3000));
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("Client not found"));
    }

    void slowLoadDoesNotReplaceNewerChoice() {
        // Likes are slow; the user picks a wave meanwhile. The late likes must be ignored.
        audio::AudioEngine engine;
        Player player(&lib, &engine);
        server.on("GET", "/users/42/likes/tracks", [](const MockRequest&) {
            return MockResponse{200, J("{'result':{'library':{'tracks':[{'id':'1'}]}}}"), 300};
        });
        server.result("POST", "/tracks/", "[" + trackJson(1, "Liked") + "]");
        server.result("POST", "/rotor/session/new",
                      "{\"radioSessionId\":\"S9\",\"sequence\":[{\"track\":" + trackJson(7, "Wave") + "}]}");
        sources::playLikes(&player, false);
        sources::playMyWave(&player);
        QVERIFY(QTest::qWaitFor([&] { return player.queueTitle() == QStringLiteral("Моя волна"); }, 3000));
        QTest::qWait(600);  // the likes response arrives now
        QCOMPARE(player.queueTitle(), QStringLiteral("Моя волна"));
        QCOMPARE(player.playlist().first().title, QStringLiteral("Wave"));
    }

    void personalPlaylistsAndRecommendations() {
        server.result("GET", "/landing3",
                      J("{'blocks':[{'type':'personal-playlists','entities':["
                        "{'type':'personal-playlist','data':{'type':'playlistOfTheDay','data':{'uid':503646255,'kind':123,'title':'Плейлист дня','trackCount':60}}},"
                        "{'type':'personal-playlist','data':{'data':{'owner':{'uid':503646255},'kind':456,'title':'Дежавю'}}}]}]}"));
        Result<QList<PlaylistRef>> r;
        lib.personalPlaylists(r.cb());
        QVERIFY(r.wait());
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.value.size(), 2);
        QCOMPARE(r.value[0].title, QStringLiteral("Плейлист дня"));
        QCOMPARE(r.value[1].ownerUid, QStringLiteral("503646255"));
        QCOMPARE(server.last("/landing3")->query.queryItemValue("blocks"), QStringLiteral("personalplaylists"));

        server.result("GET", "/users/503646255/playlists/123/recommendations",
                      "{\"batchId\":\"b\",\"tracks\":[" + trackJson(8, "Rec", 80) + "]}");
        Result<QList<Track>> recs;
        lib.playlistRecommendations(r.value[0], recs.cb());
        QVERIFY(recs.wait());
        QCOMPARE(recs.value.size(), 1);
        QCOMPARE(recs.value[0].title, QStringLiteral("Rec"));
    }

    void wheelOfWavesWithUnwrappedBody() {
        // /wheel/new answers without the usual {"result": ...} envelope.
        server.json("POST", "/wheel/new",
                    J("{'wheelId':'w1','items':[{'type':'WAVE','id':'1','data':{'wave':{'name':'Бодрое','description':'d','seeds':['mood:energetic']}}},"
                      "{'type':'OTHER','id':'2','data':{}}]}"));
        Result<QList<yandex::Wave>> r;
        lib.wheelWaves({"user:onyourwave"}, r.cb());
        QVERIFY(r.wait());
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.value.size(), 1);
        QCOMPARE(r.value[0].seeds, QStringList{"mood:energetic"});
        const QJsonObject body = QJsonDocument::fromJson(server.last("/wheel/new")->body).object();
        QCOMPARE(body.value("context").toObject().value("type").toString(), QStringLiteral("WAVE"));
    }

    void waveFeedbackFallsBackToStationEndpoint() {
        Track t = ApiClient::parseTrack(QJsonDocument::fromJson(trackJson(5, "T", 50)).object());
        // Session endpoint works: only it is used.
        server.result("POST", "/rotor/session/OK1/feedback", "\"ok\"");
        lib.waveFeedback("OK1", "user:onyourwave", "B1", yandex::WaveEvent::TrackStarted, &t);
        QVERIFY(QTest::qWaitFor([&] { return server.last("/rotor/session/OK1/feedback") != nullptr; }, 3000));
        const QJsonObject ok = QJsonDocument::fromJson(server.last("/rotor/session/OK1/feedback")->body).object();
        QCOMPARE(ok.value("batchId").toString(), QStringLiteral("B1"));
        QCOMPARE(ok.value("event").toObject().value("type").toString(), QStringLiteral("trackStarted"));
        QCOMPARE(ok.value("event").toObject().value("trackId").toString(), QStringLiteral("5:50"));

        // Session endpoint rejected: falls back to the station endpoint, and stays there.
        server.json("POST", "/rotor/session/BAD/feedback", J("{'error':{'message':'not found'}}"), 404);
        server.result("POST", "/rotor/station/user:onyourwave/feedback", "\"ok\"");
        const auto before = server.requests().size();
        auto stationCalls = [&] {
            int n = 0;
            for (qsizetype i = before; i < server.requests().size(); ++i)
                n += server.requests()[i].path == "/rotor/station/user:onyourwave/feedback";
            return n;
        };
        lib.waveFeedback("BAD", "user:onyourwave", "B2", yandex::WaveEvent::Skip, &t, 12.34);
        QVERIFY(QTest::qWaitFor([&] { return stationCalls() == 1; }, 3000));
        const MockRequest* st = server.last("/rotor/station/user:onyourwave/feedback");
        QCOMPARE(st->query.queryItemValue("batch-id"), QStringLiteral("B2"));
        const QJsonObject ev = QJsonDocument::fromJson(st->body).object();
        QCOMPARE(ev.value("type").toString(), QStringLiteral("skip"));
        QCOMPARE(ev.value("totalPlayedSeconds").toDouble(), 12.3);
        lib.waveFeedback("BAD", "user:onyourwave", "B2", yandex::WaveEvent::TrackStarted, &t);
        QVERIFY(QTest::qWaitFor([&] { return stationCalls() == 2; }, 3000));
        int sessionCalls = 0;
        for (qsizetype i = before; i < server.requests().size(); ++i)
            sessionCalls += server.requests()[i].path == "/rotor/session/BAD/feedback";
        QCOMPARE(sessionCalls, 1);  // not retried
    }

    void waveFeedbackDoesNotResendAfterServerError() {
        Track t = ApiClient::parseTrack(QJsonDocument::fromJson(trackJson(5, "T", 50)).object());
        server.json("POST", "/rotor/session/S500/feedback", J("{'error':'oops'}"), 503);
        const auto before = server.requests().size();
        bool settled = false;
        const int pending = api.pendingPosts();
        auto c = connect(&api, &ApiClient::postsSettled, this, [&] { settled = true; });
        lib.waveFeedback("S500", "user:onyourwave", "B1", yandex::WaveEvent::Skip, &t, 3);
        QCOMPARE(api.pendingPosts(), pending + 1);
        QVERIFY(QTest::qWaitFor([&] { return settled; }, 3000));
        disconnect(c);
        // A 5xx may have been counted: no second copy via the station endpoint.
        for (qsizetype i = before; i < server.requests().size(); ++i)
            QVERIFY(!server.requests()[i].path.startsWith("/rotor/station/"));
    }

    void postsSettleOnlyAfterTheFallback() {
        // Quitting waits for postsSettled; it must cover the station re-send.
        Track t = ApiClient::parseTrack(QJsonDocument::fromJson(trackJson(5, "T", 50)).object());
        server.json("POST", "/rotor/session/S404/feedback", J("{'error':{'message':'not found'}}"), 404);
        server.result("POST", "/rotor/station/user:x/feedback", "\"ok\"");
        const auto before = server.requests().size();
        int stationCallsAtSettle = -1;
        auto stationCalls = [&] {
            int n = 0;
            for (qsizetype i = before; i < server.requests().size(); ++i) n += server.requests()[i].path == "/rotor/station/user:x/feedback";
            return n;
        };
        auto c = connect(&api, &ApiClient::postsSettled, this, [&] { stationCallsAtSettle = stationCalls(); });
        lib.waveFeedback("S404", "user:x", "B1", yandex::WaveEvent::Skip, &t, 3);
        QVERIFY(QTest::qWaitFor([&] { return stationCallsAtSettle >= 0; }, 3000));
        QCOMPARE(stationCallsAtSettle, 1);
        QCOMPARE(api.pendingPosts(), 0);
        disconnect(c);
    }

    void playerReportsTrackEvents() {
        audio::AudioEngine engine;
        Player player(&lib, &engine);
        server.result("GET", "/tracks/1/download-info",
                      "[{\"codec\":\"mp3\",\"bitrateInKbps\":320,\"downloadInfoUrl\":\"" + server.baseUrl().toUtf8() + "/dl?x=1\"}]");
        server.result("GET", "/tracks/2/download-info",
                      "[{\"codec\":\"mp3\",\"bitrateInKbps\":320,\"downloadInfoUrl\":\"" + server.baseUrl().toUtf8() + "/dl?x=2\"}]");
        server.json("GET", "/dl", J("{'host':'127.0.0.1:1','path':'/p','ts':'1','s':'s'}"));
        server.result("POST", "/play-audio", "\"ok\"");
        QList<Track> tracks;
        for (int i = 1; i <= 2; ++i) tracks << ApiClient::parseTrack(QJsonDocument::fromJson(trackJson(i, "t")).object());
        QStringList log;
        player.setQueue(tracks, "W", false, {}, [&](Player::TrackEvent e, const Track& t, double) {
            log << QStringLiteral("%1:%2").arg(int(e)).arg(t.id);
        });
        player.playIndex(0);
        QVERIFY(QTest::qWaitFor([&] { return log.contains("0:1"); }, 3000));  // Started 1
        player.playIndex(1);                                                   // Skipped 1
        QVERIFY(QTest::qWaitFor([&] { return log.contains("0:2"); }, 3000));  // Started 2
        QCOMPARE(log, (QStringList{"0:1", "2:1", "0:2"}));
        player.setQueue({}, "", false);  // replacing the queue closes track 2
        QCOMPARE(log.last(), QStringLiteral("2:2"));
    }

private:
    // Real playback of short mp3s served by the mock server, for the preload tests.
    struct AudioRig {
        LocalNam nam;
        ApiClient api{&nam};
        Library lib{&api};
        audio::AudioEngine engine;
        Player player{&lib, &engine};
    };
    bool setUpAudio(AudioRig& rig, const QList<int>& ids) {
        QFile f(QStringLiteral(QIYAA_TEST_DATA "/sine440_3s.mp3"));
        if (!f.open(QIODevice::ReadOnly) || !rig.engine.init()) return false;
        rig.engine.setVolume(0);
        rig.api.setBaseUrl(server.baseUrl());
        const QByteArray mp3 = f.readAll();
        for (int id : ids) {
            server.result("GET", QStringLiteral("/tracks/%1/download-info").arg(id),
                          "[{\"codec\":\"mp3\",\"bitrateInKbps\":320,\"downloadInfoUrl\":\"" + server.baseUrl().toUtf8() +
                              "/dlinfo" + QByteArray::number(id) + "\"}]");
            server.json("GET", QStringLiteral("/dlinfo%1").arg(id),
                        "{\"host\":\"" + QUrl(server.baseUrl()).authority().toUtf8() + "\",\"path\":\"/t" +
                            QByteArray::number(id) + "\",\"ts\":\"1\",\"s\":\"s\"}");
        }
        server.onPrefix("GET", "/get-mp3/", [mp3](const MockRequest&) { return MockResponse{200, mp3}; });
        server.result("POST", "/play-audio", "\"ok\"");
        return true;
    }
    int requestsTo(const QString& path) const {
        int n = 0;
        for (const MockRequest& r : server.requests()) n += r.path == path;
        return n;
    }
    static QList<Track> numbered(const QList<int>& ids) {
        QList<Track> out;
        for (int id : ids) out << ApiClient::parseTrack(QJsonDocument::fromJson(trackJson(id, "t")).object());
        return out;
    }

private Q_SLOTS:
    void playerPreloadsAndAdvancesSeamlessly() {
        AudioRig rig;
        if (!setUpAudio(rig, {11, 12, 13})) QSKIP("no audio output");
        QStringList log;
        rig.player.setQueue(numbered({11, 12, 13}), "A", false, {}, [&](Player::TrackEvent e, const Track& t, double) {
            log << QStringLiteral("%1:%2").arg(int(e)).arg(t.id);
        });
        QSignalSpy advanced(&rig.engine, &audio::AudioEngine::trackAdvanced);
        QSignalSpy finished(&rig.engine, &audio::AudioEngine::trackFinished);
        rig.player.playIndex(0);
        // Once track 11 is downloaded, track 12 is fetched in the background.
        QVERIFY(QTest::qWaitFor([&] { return rig.player.preloadedIndex() == 1; }, 5000));
        QVERIFY(QTest::qWaitFor([&] { return rig.engine.state() == audio::AudioEngine::State::Playing; }, 3000));
        QTest::qWait(300);  // let the preload download complete
        QVERIFY(rig.player.seekTo(2.4));
        QVERIFY(advanced.wait(4000));
        QCOMPARE(finished.count(), 0);  // no stop between the tracks
        QCOMPARE(rig.player.currentIndex(), 1);
        QCOMPARE(log, (QStringList{"0:11", "1:11", "0:12"}));  // Started, Finished, Started
        QCOMPARE(requestsTo("/tracks/12/download-info"), 1);    // no second link request
        QVERIFY(rig.engine.positionSeconds() < 0.5);

        // "Next" takes the preloaded track too.
        QVERIFY(QTest::qWaitFor([&] { return rig.player.preloadedIndex() == 2; }, 5000));
        rig.player.next();
        QCOMPARE(rig.player.currentIndex(), 2);
        QCOMPARE(log.mid(3), (QStringList{"2:12", "0:13"}));    // Skipped 12, Started 13 right away
        QCOMPARE(requestsTo("/tracks/13/download-info"), 1);
        rig.player.stop();
    }

    void playerRepreloadsWhenTheQueueChanges() {
        AudioRig rig;
        if (!setUpAudio(rig, {21, 22, 23})) QSKIP("no audio output");
        rig.player.setQueue(numbered({21, 22, 23}), "A", false);
        rig.player.playIndex(0);
        QVERIFY(QTest::qWaitFor([&] { return rig.player.preloadedIndex() == 1; }, 5000));
        rig.player.removeTracks({1});  // the preloaded track is gone: 23 follows now
        QVERIFY(QTest::qWaitFor([&] { return rig.player.preloadedIndex() == 1 && requestsTo("/tracks/23/download-info") == 1; }, 5000));
        QTest::qWait(300);
        QSignalSpy advanced(&rig.engine, &audio::AudioEngine::trackAdvanced);
        QVERIFY(rig.player.seekTo(2.4));
        QVERIFY(advanced.wait(4000));
        QCOMPARE(rig.player.currentTrack()->id, QStringLiteral("23"));
        rig.player.stop();
    }

    void playerAsksEndlessSourceForMore() {
        audio::AudioEngine engine;  // not initialised: nothing actually plays
        Player player(&lib, &engine);
        QList<Track> batch;
        for (int i = 0; i < 3; ++i) batch << ApiClient::parseTrack(QJsonDocument::fromJson(trackJson(i + 1, "t")).object());
        int asked = 0;
        player.setQueue(batch, "Wave", false, [&](std::function<void(const QList<Track>&)> done) {
            ++asked;
            done({ApiClient::parseTrack(QJsonDocument::fromJson(trackJson(99, "more")).object())});
        });
        QCOMPARE(player.playlist().size(), 3);
        player.playIndex(1);  // 2 tracks left -> ask for more
        QCOMPARE(asked, 1);
        QCOMPARE(player.playlist().size(), 4);
        QCOMPARE(player.playlist().last().title, QStringLiteral("more"));
    }
};

QTEST_GUILESS_MAIN(TestLibrary)
#include "test_library.moc"
