#include "support/spec_fixtures.h"
#include "support/yandex_json.h"
#include "yandex/api_client.h"
#include "yandex/file_info.h"
#include "yandex/token.h"
#include "yandex/track_url.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QUrl>

#include <optional>

namespace {

// The "result" of a wrapped API reply in spec/fixtures/yandex.
QJsonValue ResultOf(const QString& name) {
    return QJsonDocument::fromJson(Tests::Fixture(name)).object().value(QStringLiteral("result"));
}

}  // namespace

class TestYandex : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void signsTrackUrlLikeYaamp() {
        Yandex::DownloadInfo info{"s123vla.storage.yandex.net", "/rmusic/U2FsdGVk/abc", "000612a3b4c5d", "deadbeef"};
        const QByteArray expectedSign =
            QCryptographicHash::hash("XGRlBW9FXlekgbPrRHuSiArmusic/U2FsdGVk/abcdeadbeef", QCryptographicHash::Md5)
                .toHex();
        QCOMPARE(
            Yandex::BuildTrackUrl(info).toString(),
            QStringLiteral("https://s123vla.storage.yandex.net/get-mp3/%1/000612a3b4c5d/rmusic/U2FsdGVk/abc")
                .arg(QString::fromLatin1(expectedSign))
        );
    }
    void downloadVariantsAreParsedAndTheBestIsPicked_data() {
        QTest::addColumn<QString>("name");
        for (const char* name : {"variants", "previews-only", "empty"}) {
            QTest::newRow(name) << QStringLiteral("tracks-download-info/%1").arg(name);
        }
    }
    void downloadVariantsAreParsedAndTheBestIsPicked() {
        QFETCH(QString, name);
        const QList<Yandex::DownloadVariant> variants = Yandex::ParseDownloadVariants(ResultOf(name).toArray());
        QCOMPARE(Tests::Json(Tests::VariantsJson(variants, Yandex::PickBestVariant(variants))), Tests::Expected(name));
    }
    void storageReplyGivesTheSignedLink_data() {
        QTest::addColumn<QString>("name");
        for (const char* name : {"ok", "number-ts", "xml"}) {
            QTest::newRow(name) << QStringLiteral("storage-download-info/%1").arg(name);
        }
    }
    void storageReplyGivesTheSignedLink() {
        QFETCH(QString, name);
        const std::optional<Yandex::DownloadInfo> info = Yandex::ParseDownloadInfo(Tests::Fixture(name));
        const QJsonObject actual = info ? Tests::ToJson(*info) : QJsonObject{{QStringLiteral("invalid"), true}};
        QCOMPARE(Tests::Json(actual), Tests::Expected(name));
    }
    void fileInfoRepliesAreParsedOrFallBack_data() {
        QTest::addColumn<QString>("name");
        for (const char* name :
             {"lq-he-aac-mp4", "nq-mp3", "encraw", "no-url", "403-not-allowed", "429-concurrency-limit"}) {
            QTest::newRow(name) << QStringLiteral("get-file-info/%1").arg(name);
        }
    }
    void fileInfoRepliesAreParsedOrFallBack() {
        QFETCH(QString, name);
        std::optional<Yandex::FileInfo> info;
        if (Tests::FixtureStatus(name) == 200) {
            info = Yandex::ParseFileInfo(ResultOf(name));
        }
        const QJsonObject actual = info ? Tests::ToJson(*info) : QJsonObject{{QStringLiteral("fallback"), true}};
        QCOMPARE(Tests::Json(actual), Tests::Expected(name));
    }
    // The worked example of spec/player/quality.md.
    void signsFileInfoLikeTheSpecExample() {
        const Yandex::FileInfoRequest request{
            QStringLiteral("155647609"),
            QStringLiteral("lq"),
            {QStringLiteral("he-aac-mp4"), QStringLiteral("mp3")},
            QStringLiteral("raw")
        };
        QCOMPARE(
            Yandex::SignFileInfo(request, 1'760'000'000), QStringLiteral("keefcyb+nfzl1cRn8glR+ZyzpQf/h2gSrMF0fHIOHpQ")
        );
        QUrl url(QStringLiteral("https://api.music.yandex.net/get-file-info"));
        url.setQuery(Yandex::FileInfoQuery(request, 1'760'000'000), QUrl::StrictMode);
        QCOMPARE(
            url.toEncoded(),
            QByteArray(
                "https://api.music.yandex.net/get-file-info?ts=1760000000&trackId=155647609"
                "&quality=lq&codecs=he-aac-mp4%2Cmp3&transports=raw"
                "&sign=keefcyb%2Bnfzl1cRn8glR%2BZyzpQf%2Fh2gSrMF0fHIOHpQ"
            )
        );
    }
    void onlyAPlainFileInAnAskedCodecIsUsable() {
        const Yandex::FileInfoRequest request{
            QStringLiteral("1"),
            QStringLiteral("lq"),
            {QStringLiteral("he-aac-mp4"), QStringLiteral("mp3")},
            QStringLiteral("raw")
        };
        Yandex::FileInfo info;
        info.codec = QStringLiteral("he-aac-mp4");
        info.transport = QStringLiteral("raw");
        QVERIFY(Yandex::IsUsable(info, request));
        info.transport = QStringLiteral("encraw");
        QVERIFY(!Yandex::IsUsable(info, request));
        info.transport = QStringLiteral("raw");
        info.codec = QStringLiteral("flac-mp4");
        QVERIFY(!Yandex::IsUsable(info, request));
    }
    // Against the real API when QIYAA_YANDEX_TOKEN holds a token: are the signing key and the
    // client header still accepted (spec/player/quality.md)? A fallback would mean they are not.
    void economyLinkFromTheRealApi() {
        const QString token = qEnvironmentVariable("QIYAA_YANDEX_TOKEN");
        if (token.isEmpty()) {
            QSKIP("QIYAA_YANDEX_TOKEN is not set");
        }
        QNetworkAccessManager network;
        Yandex::ApiClient api(&network);
        api.setToken(token);
        QSignalSpy failures(&api, &Yandex::ApiClient::requestFailed);
        std::optional<Yandex::ResolvedUrl> link;
        Yandex::RequestError error;
        api.resolveTrackUrl(
            QStringLiteral("155647609"),
            Yandex::Quality::Economy,
            [&](const Yandex::ResolvedUrl& resolved, const Yandex::RequestError& failed) {
                link = resolved;
                error = failed;
            }
        );
        QVERIFY(QTest::qWaitFor([&] { return link.has_value(); }, 20'000));
        QVERIFY2(!error.isError(), qPrintable(error.text));
        QCOMPARE(failures.count(), 0);
        QCOMPARE(link->codec, QStringLiteral("he-aac-mp4"));
        QCOMPARE(link->bitrateKbps, 64);
        QVERIFY(link->url.host().endsWith(QLatin1String(".strm.yandex.net")));
    }
    void qualityNamesRoundTrip() {
        for (const Yandex::Quality quality : {Yandex::Quality::Standard, Yandex::Quality::Economy}) {
            QCOMPARE(Yandex::QualityFromKey(Yandex::QualityKey(quality)), quality);
        }
        QCOMPARE(Yandex::QualityKey(Yandex::Quality::Economy), QStringLiteral("economy"));
        QCOMPARE(Yandex::QualityFromKey(QStringLiteral("lossless")), std::nullopt);
    }
    void normalizesTokens() {
        QCOMPARE(Yandex::NormalizeToken("  y0_AgAAAAAtest_token-123\n"), QStringLiteral("y0_AgAAAAAtest_token-123"));
        QCOMPARE(Yandex::NormalizeToken("\"y0_AgAAAAAtest_token\""), QStringLiteral("y0_AgAAAAAtest_token"));
        QCOMPARE(
            Yandex::NormalizeToken("{\"access_token\":\"y0_AgAAAAAtest_token\",\"expires_in\":1}"),
            QStringLiteral("y0_AgAAAAAtest_token")
        );
        QCOMPARE(
            Yandex::NormalizeToken("https://music.yandex.ru/#access_token=y0_AgAAAAAtest_token&token_type=bearer"),
            QStringLiteral("y0_AgAAAAAtest_token")
        );
        QCOMPARE(Yandex::NormalizeToken(""), QString());
        QCOMPARE(Yandex::NormalizeToken("not a token at all"), QString());
    }
    void tracksAreParsed_data() {
        QTest::addColumn<QString>("name");
        for (const char* name :
             {"two-tracks",
              "with-unavailable",
              "version-and-artists",
              "cover-from-album",
              "cover-order",
              "all-unavailable"}) {
            QTest::newRow(name) << QStringLiteral("tracks/%1").arg(name);
        }
    }
    void tracksAreParsed() {
        QFETCH(QString, name);
        QList<Yandex::Track> tracks;
        for (const QJsonValue& value : ResultOf(name).toArray()) {
            tracks << Yandex::ApiClient::ParseTrack(value);
        }
        QCOMPARE(Tests::Json(Tests::TracksJson(tracks)), Tests::Expected(name));
    }
    void trackDisplayTitleAndCoverLink() {
        const Yandex::Track live =
            Yandex::ApiClient::ParseTrack(ResultOf("tracks/version-and-artists").toArray().first());
        QCOMPARE(live.displayTitle(), QStringLiteral("A, B - Song (Live)"));
        QVERIFY(live.coverUrl().isEmpty());
        const Yandex::Track fromAlbum =
            Yandex::ApiClient::ParseTrack(ResultOf("tracks/cover-from-album").toArray().first());
        QCOMPARE(
            fromAlbum.coverUrl(200).toString(),
            QStringLiteral("https://avatars.yandex.net/get-music-content/1/a/200x200")
        );
    }
};

QTEST_GUILESS_MAIN(TestYandex)
#include "yandex_test.moc"
