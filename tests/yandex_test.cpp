#include "support/mock_http_server.h"
#include "support/spec_fixtures.h"
#include "support/yandex_json.h"
#include "yandex/api_client.h"
#include "yandex/download.h"
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

#include <memory>
#include <optional>

namespace {

// The "result" of a wrapped API reply in spec/fixtures/yandex.
QJsonValue ResultOf(const QString& name) {
    return QJsonDocument::fromJson(Tests::Fixture(name)).object().value(QStringLiteral("result"));
}

struct DownloadLog {
    QByteArray bytes;
    QList<Yandex::RequestError> finishes;
    bool receivedAfterFinish = false;
};

void Record(Yandex::Download& download, DownloadLog& log) {
    QObject::connect(&download, &Yandex::Download::received, [&log](const QByteArray& bytes) {
        log.receivedAfterFinish = log.receivedAfterFinish || !log.finishes.isEmpty();
        log.bytes += bytes;
    });
    QObject::connect(&download, &Yandex::Download::finished, [&log](const Yandex::RequestError& error) {
        log.finishes << error;
    });
}

QByteArray AudioBytes(int size) {
    QByteArray bytes(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i) {
        bytes[i] = static_cast<char>((i * 7 + i / 251) & 0xff);
    }
    return bytes;
}

}  // namespace

class TestYandex : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void signsTrackUrlLikeYaamp() {
        Yandex::DownloadInfo info{"s123vla.storage.yandex.net", "/rmusic/U2FsdGVk/abc", "000612a3b4c5d", "deadbeef"};
        const QByteArray signedText = "XGRlBW9FXlekgbPrRHuSiArmusic/U2FsdGVk/abcdeadbeef";
        const QString expectedSign =
            QString::fromLatin1(QCryptographicHash::hash(signedText, QCryptographicHash::Md5).toHex());
        const QString expectedUrl =
            QStringLiteral("https://s123vla.storage.yandex.net/get-mp3/%1/000612a3b4c5d/rmusic/U2FsdGVk/abc")
                .arg(expectedSign);
        QCOMPARE(Yandex::BuildTrackUrl(info).toString(), expectedUrl);
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
    void supplementRepliesGiveThePlainText_data() {  // LYR-02
        QTest::addColumn<QString>("name");
        for (const char* name : {"with-lyrics", "without-lyrics", "no-rights", "400-validate"}) {
            QTest::newRow(name) << QStringLiteral("tracks-supplement/%1").arg(name);
        }
    }

    void supplementRepliesGiveThePlainText() {
        QFETCH(QString, name);
        Tests::MockHttpServer server;
        Tests::LocalNetworkAccessManager network;
        Yandex::ApiClient api(&network);
        api.setBaseUrl(server.baseUrl());
        api.setToken(QStringLiteral("test-token"));
        server.fixture("GET", QStringLiteral("/tracks/33311009/supplement"), name);
        QSignalSpy failures(&api, &Yandex::ApiClient::requestFailed);
        std::optional<std::optional<QString>> text;
        Yandex::RequestError error;
        api.lyricsText(
            QStringLiteral("33311009:4"),
            [&](const std::optional<QString>& lyrics, const Yandex::RequestError& failed) {
                text = lyrics;
                error = failed;
            }
        );
        QVERIFY(QTest::qWaitFor([&] { return text.has_value(); }, 5'000));
        QVERIFY(server.last(QStringLiteral("/tracks/33311009/supplement")));
        if (Tests::FixtureStatus(name) != 200) {
            QVERIFY(error.isError());
            const QString mismatch = Tests::CheckError(error.text, name);
            QVERIFY2(mismatch.isEmpty(), qPrintable(mismatch));
            QCOMPARE(failures.count(), 1);
            return;
        }
        QVERIFY(!error.isError());
        QCOMPARE(failures.count(), 0);
        const QJsonObject actual{{QStringLiteral("lyrics"), *text ? QJsonValue(**text) : QJsonValue()}};
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
    void trackCoverLink() {
        const Yandex::Track live =
            Yandex::ApiClient::ParseTrack(ResultOf("tracks/version-and-artists").toArray().first());
        QVERIFY(live.coverUrl().isEmpty());
        const Yandex::Track fromAlbum =
            Yandex::ApiClient::ParseTrack(ResultOf("tracks/cover-from-album").toArray().first());
        QCOMPARE(
            fromAlbum.coverUrl(200).toString(),
            QStringLiteral("https://avatars.yandex.net/get-music-content/1/a/200x200")
        );
    }
    void aDownloadDeliversTheBytesInOrderAndFinishesOnce() {
        Tests::MockHttpServer server;
        const QByteArray body = AudioBytes(300'000);
        server.on("GET", "/file", [body](const Tests::MockRequest&) { return Tests::MockResponse{200, body}; });
        QNetworkAccessManager network;
        Yandex::ApiClient api(&network);
        const std::unique_ptr<Yandex::Download> download = api.download(QUrl(server.baseUrl() + "/file"));
        DownloadLog log;
        Record(*download, log);
        QVERIFY(QTest::qWaitFor([&] { return !log.finishes.isEmpty(); }, 5'000));
        QTest::qWait(50);
        QCOMPARE(log.finishes.size(), 1);
        QVERIFY(!log.finishes.first().isError());
        QVERIFY(log.bytes == body);
        QVERIFY(!log.receivedAfterFinish);
    }
    void aDownloadCutShortIsANetworkFailure() {
        Tests::MockHttpServer server;
        const QByteArray body = AudioBytes(50'000);
        server.on("GET", "/file", [body](const Tests::MockRequest&) {
            return Tests::MockResponse{200, body, 0, 20'000};
        });
        QNetworkAccessManager network;
        Yandex::ApiClient api(&network);
        const std::unique_ptr<Yandex::Download> download = api.download(QUrl(server.baseUrl() + "/file"));
        DownloadLog log;
        Record(*download, log);
        QVERIFY(QTest::qWaitFor([&] { return !log.finishes.isEmpty(); }, 5'000));
        QCOMPARE(log.finishes.first().kind, Yandex::RequestError::Kind::Network);
        QVERIFY(!log.finishes.first().text.isEmpty());
        QVERIFY(log.bytes == body.left(20'000));
    }
    void anErrorPageIsNotAudio() {
        Tests::MockHttpServer server;
        server.json("GET", "/file", "{\"error\":\"not found\"}", 404);
        QNetworkAccessManager network;
        Yandex::ApiClient api(&network);
        const std::unique_ptr<Yandex::Download> download = api.download(QUrl(server.baseUrl() + "/file"));
        DownloadLog log;
        Record(*download, log);
        QVERIFY(QTest::qWaitFor([&] { return !log.finishes.isEmpty(); }, 5'000));
        QCOMPARE(log.finishes.first().kind, Yandex::RequestError::Kind::Http);
        QCOMPARE(log.finishes.first().httpStatus, 404);
        QVERIFY(log.bytes.isEmpty());
    }
    void aDownloadSendsNoToken() {
        Tests::MockHttpServer server;
        server.on("GET", "/file", [](const Tests::MockRequest&) { return Tests::MockResponse{200, "abc"}; });
        QNetworkAccessManager network;
        Yandex::ApiClient api(&network);
        api.setToken(QStringLiteral("secret"));
        const std::unique_ptr<Yandex::Download> download = api.download(QUrl(server.baseUrl() + "/file"));
        DownloadLog log;
        Record(*download, log);
        QVERIFY(QTest::qWaitFor([&] { return !log.finishes.isEmpty(); }, 5'000));
        QVERIFY(server.last(QStringLiteral("/file")));
        QVERIFY(!server.last(QStringLiteral("/file"))->headers.contains("authorization"));
    }
    void deletingADownloadAbortsItWithoutSignals() {
        Tests::MockHttpServer server;
        server.on("GET", "/file", [](const Tests::MockRequest&) {
            return Tests::MockResponse{200, AudioBytes(1'000), 300};
        });
        QNetworkAccessManager network;
        Yandex::ApiClient api(&network);
        std::unique_ptr<Yandex::Download> download = api.download(QUrl(server.baseUrl() + "/file"));
        DownloadLog log;
        Record(*download, log);
        QVERIFY(QTest::qWaitFor([&] { return server.last(QStringLiteral("/file")) != nullptr; }, 5'000));
        download.reset();
        QTest::qWait(500);
        QVERIFY(log.finishes.isEmpty());
        QVERIFY(log.bytes.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestYandex)
#include "yandex_test.moc"
