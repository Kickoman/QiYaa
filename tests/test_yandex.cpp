#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

#include "yandex/ApiClient.h"
#include "yandex/Token.h"
#include "yandex/TrackUrl.h"

using namespace qiyaa::yandex;

class TestYandex : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void signsTrackUrlLikeYaamp() {
        DownloadInfo info{"s123vla.storage.yandex.net", "/rmusic/U2FsdGVk/abc", "000612a3b4c5d", "deadbeef"};
        const QByteArray expectedSign =
            QCryptographicHash::hash("XGRlBW9FXlekgbPrRHuSiArmusic/U2FsdGVk/abcdeadbeef", QCryptographicHash::Md5).toHex();
        QCOMPARE(buildTrackUrl(info).toString(),
                 QStringLiteral("https://s123vla.storage.yandex.net/get-mp3/%1/000612a3b4c5d/rmusic/U2FsdGVk/abc")
                     .arg(QString::fromLatin1(expectedSign)));
    }
    void picksBestFullMp3() {
        const QJsonArray arr = QJsonDocument::fromJson("[\n"
            "            {\"codec\":\"mp3\",\"bitrateInKbps\":320,\"preview\":true,\"downloadInfoUrl\":\"https://x/a?sign=1\"},\n"
            "            {\"codec\":\"aac\",\"bitrateInKbps\":256,\"preview\":false,\"downloadInfoUrl\":\"https://x/b?sign=1\"},\n"
            "            {\"codec\":\"mp3\",\"bitrateInKbps\":192,\"preview\":false,\"downloadInfoUrl\":\"https://x/c?sign=1\"},\n"
            "            {\"codec\":\"mp3\",\"bitrateInKbps\":320,\"preview\":false,\"downloadInfoUrl\":\"https://x/d?sign=1\"}\n"
            "        ]").array();
        DownloadVariant v;
        QVERIFY(pickBestVariant(parseDownloadVariants(arr), &v));
        QCOMPARE(v.bitrateKbps, 320);
        QCOMPARE(v.downloadInfoUrl.path(), QStringLiteral("/d"));
    }
    void parsesDownloadInfoJson() {
        DownloadInfo info;
        QVERIFY(parseDownloadInfo("{\"s\":\"abc\",\"ts\":\"0005\",\"path\":\"/p/q\",\"host\":\"h.net\",\"regional-host\":[]}", &info));
        QCOMPARE(info.host, QStringLiteral("h.net"));
        QVERIFY(!parseDownloadInfo("<xml/>", &info));
    }
    void normalizesTokens() {
        QCOMPARE(normalizeToken("  y0_AgAAAAAtest_token-123\n"), QStringLiteral("y0_AgAAAAAtest_token-123"));
        QCOMPARE(normalizeToken("\"y0_AgAAAAAtest_token\""), QStringLiteral("y0_AgAAAAAtest_token"));
        QCOMPARE(normalizeToken("{\"access_token\":\"y0_AgAAAAAtest_token\",\"expires_in\":1}"),
                 QStringLiteral("y0_AgAAAAAtest_token"));
        QCOMPARE(normalizeToken("https://music.yandex.ru/#access_token=y0_AgAAAAAtest_token&token_type=bearer"),
                 QStringLiteral("y0_AgAAAAAtest_token"));
        QCOMPARE(normalizeToken(""), QString());
        QCOMPARE(normalizeToken("not a token at all"), QString());
    }
    void parsesTrack() {
        const auto doc = QJsonDocument::fromJson("{\"id\":\"12345\",\"title\":\"Song\",\"version\":\"Live\",\n"
            "            \"artists\":[{\"name\":\"A\"},{\"name\":\"B\"}],\"albums\":[{\"id\":777}],\"durationMs\":201000,\"available\":true}");
        const Track t = ApiClient::parseTrack(doc.object());
        QCOMPARE(t.id, QStringLiteral("12345"));
        QCOMPARE(t.albumId, QStringLiteral("777"));
        QCOMPARE(t.displayTitle(), QStringLiteral("A, B - Song (Live)"));
        QCOMPARE(t.durationMs, 201000);
    }
};

QTEST_GUILESS_MAIN(TestYandex)
#include "test_yandex.moc"
