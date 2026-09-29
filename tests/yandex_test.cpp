#include "support/spec_fixtures.h"
#include "support/yandex_json.h"
#include "yandex/api_client.h"
#include "yandex/token.h"
#include "yandex/track_url.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>
#include <QTest>

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
    void downloadVariantsAreParsedAndTheBestIsPicked_data() {
        QTest::addColumn<QString>("name");
        for (const char* name : {"variants", "previews-only", "empty"}) {
            QTest::newRow(name) << QStringLiteral("tracks-download-info/%1").arg(name);
        }
    }
    void downloadVariantsAreParsedAndTheBestIsPicked() {
        QFETCH(QString, name);
        const QList<Yandex::DownloadVariant> variants =
            Yandex::ParseDownloadVariants(ResultOf(name).toArray());
        QCOMPARE(
            Tests::Json(Tests::VariantsJson(variants, Yandex::PickBestVariant(variants))),
            Tests::Expected(name)
        );
    }
    void storageReplyGivesTheSignedLink_data() {
        QTest::addColumn<QString>("name");
        for (const char* name : {"ok", "number-ts", "xml"}) {
            QTest::newRow(name) << QStringLiteral("storage-download-info/%1").arg(name);
        }
    }
    void storageReplyGivesTheSignedLink() {
        QFETCH(QString, name);
        const std::optional<Yandex::DownloadInfo> info =
            Yandex::ParseDownloadInfo(Tests::Fixture(name));
        const QJsonObject actual =
            info ? Tests::ToJson(*info) : QJsonObject{{QStringLiteral("invalid"), true}};
        QCOMPARE(Tests::Json(actual), Tests::Expected(name));
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
    void tracksAreParsed_data() {
        QTest::addColumn<QString>("name");
        for (const char* name :
             {"two-tracks", "with-unavailable", "version-and-artists", "cover-from-album"}) {
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
