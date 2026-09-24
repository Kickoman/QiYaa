// Yandex Music layer against a local mock server: request shapes and parsing
// for every source the menu offers, likes, waves, search and device login.
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
