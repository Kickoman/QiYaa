#include "yandex/api_client.h"
#include "yandex/token.h"
#include "yandex/track_url.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

class TestYandex : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void signsTrackUrlLikeYaamp() {
        Yandex::DownloadInfo info{
            "s123vla.storage.yandex.net", "/rmusic/U2FsdGVk/abc", "000612a3b4c5d", "deadbeef"
        };
        const QByteArray expectedSign =
            QCryptographicHash::hash(
                "XGRlBW9FXlekgbPrRHuSiArmusic/U2FsdGVk/abcdeadbeef", QCryptographicHash::Md5
            )
                .toHex();
        QCOMPARE(
            Yandex::BuildTrackUrl(info).toString(),
            QStringLiteral(
                "https://s123vla.storage.yandex.net/get-mp3/%1/000612a3b4c5d/rmusic/U2FsdGVk/abc"
            )
                .arg(QString::fromLatin1(expectedSign))
        );
    }
    void picksBestFullMp3() {
        const QJsonArray arr =
            QJsonDocument::fromJson("[\n"
                                    "            "
                                    "{\"codec\":\"mp3\",\"bitrateInKbps\":320,\"preview\":true,"
                                    "\"downloadInfoUrl\":\"https://x/a?sign=1\"},\n"
                                    "            "
                                    "{\"codec\":\"aac\",\"bitrateInKbps\":256,\"preview\":false,"
                                    "\"downloadInfoUrl\":\"https://x/b?sign=1\"},\n"
                                    "            "
                                    "{\"codec\":\"mp3\",\"bitrateInKbps\":192,\"preview\":false,"
                                    "\"downloadInfoUrl\":\"https://x/c?sign=1\"},\n"
                                    "            "
                                    "{\"codec\":\"mp3\",\"bitrateInKbps\":320,\"preview\":false,"
                                    "\"downloadInfoUrl\":\"https://x/d?sign=1\"}\n"
                                    "        ]")
                .array();
        Yandex::DownloadVariant v;
        QVERIFY(Yandex::PickBestVariant(Yandex::ParseDownloadVariants(arr), &v));
        QCOMPARE(v.bitrateKbps, 320);
        QCOMPARE(v.downloadInfoUrl.path(), QStringLiteral("/d"));
    }
    void parsesDownloadInfoJson() {
        Yandex::DownloadInfo info;
        QVERIFY(Yandex::ParseDownloadInfo(
            "{\"s\":\"abc\",\"ts\":\"0005\",\"path\":\"/p/"
            "q\",\"host\":\"h.net\",\"regional-host\":[]}",
            &info
        ));
        QCOMPARE(info.host, QStringLiteral("h.net"));
        QVERIFY(!Yandex::ParseDownloadInfo("<xml/>", &info));
    }
    void normalizesTokens() {
        QCOMPARE(
            Yandex::NormalizeToken("  y0_AgAAAAAtest_token-123\n"),
            QStringLiteral("y0_AgAAAAAtest_token-123")
        );
        QCOMPARE(
            Yandex::NormalizeToken("\"y0_AgAAAAAtest_token\""),
            QStringLiteral("y0_AgAAAAAtest_token")
        );
        QCOMPARE(
            Yandex::NormalizeToken("{\"access_token\":\"y0_AgAAAAAtest_token\",\"expires_in\":1}"),
            QStringLiteral("y0_AgAAAAAtest_token")
        );
        QCOMPARE(
            Yandex::NormalizeToken(
                "https://music.yandex.ru/#access_token=y0_AgAAAAAtest_token&token_type=bearer"
            ),
            QStringLiteral("y0_AgAAAAAtest_token")
        );
        QCOMPARE(Yandex::NormalizeToken(""), QString());
        QCOMPARE(Yandex::NormalizeToken("not a token at all"), QString());
    }
    void parsesTrack() {
        const auto doc =
            QJsonDocument::fromJson("{\"id\":\"12345\",\"title\":\"Song\",\"version\":\"Live\",\n"
                                    "            "
                                    "\"artists\":[{\"name\":\"A\"},{\"name\":\"B\"}],\"albums\":[{"
                                    "\"id\":777}],\"durationMs\":201000,\"available\":true}");
        const Yandex::Track track = Yandex::ApiClient::ParseTrack(doc.object());
        QCOMPARE(track.id, QStringLiteral("12345"));
        QCOMPARE(track.albumId, QStringLiteral("777"));
        QCOMPARE(track.displayTitle(), QStringLiteral("A, B - Song (Live)"));
        QCOMPARE(track.durationMs, 201000);
        QVERIFY(track.coverUrl().isEmpty());
    }
    void parsesAlbumDetailsAndCover() {
        const auto doc = QJsonDocument::fromJson(
            "{\"id\":1,\"title\":\"T\",\"albums\":[{\"id\":2,\"title\":\"Звезда\","
            "\"year\":1989,\"genre\":\"rusrock\",\"coverUri\":\"avatars.yandex.net/"
            "get-music-content/1/a/%%\"}]}"
        );
        const Yandex::Track track = Yandex::ApiClient::ParseTrack(doc.object());
        QCOMPARE(track.albumTitle, QStringLiteral("Звезда"));
        QCOMPARE(track.year, 1989);
        QCOMPARE(
            track.coverUrl(200).toString(),
            QStringLiteral("https://avatars.yandex.net/get-music-content/1/a/200x200")
        );
    }
};

QTEST_GUILESS_MAIN(TestYandex)
#include "yandex_test.moc"
