#include "core/track.h"
#include "core/track_source.h"
#include "sources/yandex/source.h"
#include "sources/yandex/tracks.h"
#include "support/mock_http_server.h"
#include "support/spec_fixtures.h"
#include "support/yandex_json.h"
#include "yandex/api_client.h"
#include "yandex/file_info.h"
#include "yandex/library.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QUrl>

#include <optional>
#include <utility>
#include <variant>

namespace {

const QStringList kTrackFixtures = {
    QStringLiteral("tracks/two-tracks"),
    QStringLiteral("tracks/with-unavailable"),
    QStringLiteral("tracks/version-and-artists"),
    QStringLiteral("tracks/cover-from-album"),
    QStringLiteral("tracks/cover-order"),
    QStringLiteral("albums-with-tracks/two-volumes"),
};

QJsonValue ResultOf(const QString& name) {
    return QJsonDocument::fromJson(Tests::Fixture(name)).object().value(QStringLiteral("result"));
}

QList<Yandex::Track> FixtureTracks() {
    QList<Yandex::Track> tracks;
    for (const QString& name : kTrackFixtures) {
        const QJsonValue result = ResultOf(name);
        QJsonArray items = result.isArray() ? result.toArray() : QJsonArray();
        if (result.isObject()) {
            for (const QJsonValue& volume : result.toObject().value(QStringLiteral("volumes")).toArray()) {
                for (const QJsonValue& track : volume.toArray()) {
                    items << track;
                }
            }
        }
        for (const QJsonValue& item : items) {
            tracks << Yandex::ApiClient::ParseTrack(item);
        }
    }
    return tracks;
}

Yandex::Track Sample(const QString& id, const QString& albumId) {
    Yandex::Track track;
    track.id = id;
    track.albumId = albumId;
    track.title = QStringLiteral("Группа крови");
    track.artists = {QStringLiteral("Кино")};
    track.coverUri = QStringLiteral("avatars.yandex.net/get-music-content/1/a/%%");
    return track;
}

Core::TOpenResult Open(Core::TrackSource& source, const Core::Track& track) {
    std::optional<Core::TOpenResult> result;
    source.open(track, [&](Core::TOpenResult opened) { result = std::move(opened); });
    QTest::qWaitFor([&] { return result.has_value(); }, 5'000);
    return result ? std::move(*result) : Core::TOpenResult(Core::TrackFailure{Core::FailureKind::Track, "timeout"});
}

Core::LyricsAnswer AskLyrics(Core::TrackSource& source, const Core::Track& track, bool* answeredInside = nullptr) {
    std::optional<Core::LyricsAnswer> answer;
    source.lyrics(track, [&](Core::LyricsAnswer lyrics) { answer = std::move(lyrics); });
    if (answeredInside) {
        *answeredInside = answer.has_value();
    }
    if (!QTest::qWaitFor([&] { return answer.has_value(); }, 5'000)) {
        return {.status = Core::LyricsAnswer::Status::Failed, .from = Core::LyricsFrom::ThirdParty};
    }
    return *answer;
}

}  // namespace

class TestYandexSource : public QObject {
    Q_OBJECT
private:
    Tests::MockHttpServer server;
    Tests::LocalNetworkAccessManager networkManager;
    Yandex::ApiClient api{&networkManager};
    Yandex::Library library{&api};

    qsizetype requestsTo(const QString& prefix, qsizetype from = 0) const {
        qsizetype count = 0;
        for (qsizetype i = from; i < server.requests().size(); ++i) {
            count += server.requests()[i].path.startsWith(prefix) ? 1 : 0;
        }
        return count;
    }

private Q_SLOTS:
    void initTestCase() {
        api.setBaseUrl(server.baseUrl());
        api.setToken(QStringLiteral("test-token"));
    }

    void aTrackWithAnAlbumIsKeyedByTrackAndAlbum() {
        const Core::Track track = YandexSource::TrackOf(Sample(QStringLiteral("123"), QStringLiteral("7")));
        QCOMPARE(track.key.source, QStringLiteral("yandex"));
        QCOMPARE(track.key.id, QStringLiteral("123:7"));
        QCOMPARE(YandexSource::CatalogId(track.key), QStringLiteral("123"));
        QCOMPARE(YandexSource::KeyOf(QStringLiteral("123"), QStringLiteral("7")), track.key);
    }

    void aTrackWithoutAnAlbumIsKeyedByTrackAlone() {
        const Core::Track track = YandexSource::TrackOf(Sample(QStringLiteral("123"), QString()));
        QCOMPARE(track.key.id, QStringLiteral("123"));
        QCOMPARE(YandexSource::CatalogId(track.key), QStringLiteral("123"));
    }

    void everySpecTrackComesBackWhole() {
        const QList<Yandex::Track> tracks = FixtureTracks();
        QVERIFY(tracks.size() >= 6);
        for (const Yandex::Track& track : tracks) {
            const std::optional<Yandex::Track> back = YandexSource::YandexTrackOf(YandexSource::TrackOf(track));
            QVERIFY(back);
            QCOMPARE(Tests::ToJson(*back), Tests::ToJson(track));
        }
        QCOMPARE(YandexSource::TracksOf(tracks).size(), tracks.size());
    }

    void anotherSourcesTrackIsNoneOfYandexs() {
        Core::Track local;
        local.key = {QStringLiteral("local"), QStringLiteral("/music/123:7.mp3")};
        local.cover = {QStringLiteral("avatars.yandex.net/get-music-content/1/a/%%")};
        QVERIFY(!YandexSource::YandexTrackOf(local));
        QVERIFY(YandexSource::CatalogId(local.key).isEmpty());
        const YandexSource::Source source(&library);
        QVERIFY(source.coverUrl(local, 400).isEmpty());
        QVERIFY(source.webPage(local).isEmpty());
    }

    void coverAndPageAreTheClientsOwn() {
        YandexSource::Source source(&library);
        Core::SourceRegistry registry;
        registry.add(YandexSource::kSourceName, &source);
        QList<Yandex::Track> tracks = FixtureTracks();
        tracks << Sample(QStringLiteral("123"), QString());
        for (const Yandex::Track& track : tracks) {
            const Core::Track coreTrack = YandexSource::TrackOf(track);
            QCOMPARE(registry.coverUrl(coreTrack, 400), track.coverUrl(400));
            QCOMPARE(registry.webPage(coreTrack), track.webUrl());
        }
        Core::Track unknown;
        unknown.key = {QStringLiteral("nowhere"), QStringLiteral("1")};
        QVERIFY(registry.find(QStringLiteral("nowhere")) == nullptr);
        QVERIFY(registry.coverUrl(unknown, 400).isEmpty());
        QVERIFY(registry.webPage(unknown).isEmpty());
    }

    void anOpenedTrackDownloadsOnlyOnceStarted() {
        const QByteArray body(4'000, 'a');
        server.audioTracks({QStringLiteral("5")}, body);
        YandexSource::Source source(&library);
        Core::TOpenResult result =
            Open(source, YandexSource::TrackOf(Sample(QStringLiteral("5"), QStringLiteral("7"))));
        auto* opened = std::get_if<Core::OpenedTrack>(&result);
        QVERIFY(opened);
        QCOMPARE(opened->bitrateKbps, 320);
        QCOMPARE(opened->codec, QStringLiteral("mp3"));
        QVERIFY(opened->link.path().startsWith(QStringLiteral("/get-mp3/")));
        QTest::qWait(100);
        QCOMPARE(requestsTo(QStringLiteral("/get-mp3/")), 0);
        QByteArray bytes;
        std::optional<std::optional<Core::TrackFailure>> finish;
        connect(opened->download.get(), &Core::TrackDownload::received, this, [&](const QByteArray& received) {
            bytes += received;
        });
        connect(
            opened->download.get(),
            &Core::TrackDownload::finished,
            this,
            [&](const std::optional<Core::TrackFailure>& failure) { finish = failure; }
        );
        opened->download->start();
        QVERIFY(QTest::qWaitFor([&] { return finish.has_value(); }, 5'000));
        QVERIFY(!finish->has_value());
        QVERIFY(bytes == body);
        QCOMPARE(requestsTo(QStringLiteral("/get-mp3/")), 1);
    }

    void aTrackThatCannotOpenSaysWhy() {
        server.json("GET", QStringLiteral("/tracks/41/download-info"), "{}", 404);
        server.json("GET", QStringLiteral("/tracks/42/download-info"), "{}", 401);
        YandexSource::Source source(&library);
        const Core::TOpenResult missing = Open(source, YandexSource::TrackOf(Sample(QStringLiteral("41"), {})));
        QVERIFY(std::holds_alternative<Core::TrackFailure>(missing));
        QCOMPARE(std::get<Core::TrackFailure>(missing).kind, Core::FailureKind::Track);
        QVERIFY(!std::get<Core::TrackFailure>(missing).text.isEmpty());
        const Core::TOpenResult denied = Open(source, YandexSource::TrackOf(Sample(QStringLiteral("42"), {})));
        QVERIFY(std::holds_alternative<Core::TrackFailure>(denied));
        QCOMPARE(std::get<Core::TrackFailure>(denied).kind, Core::FailureKind::Auth);
    }

    void onlyAStartReportsAPlay() {
        server.fixture("POST", "/play-audio", "play-audio/ok");
        YandexSource::Source source(&library);
        const Core::Track track = YandexSource::TrackOf(Sample(QStringLiteral("5"), QStringLiteral("7")));
        const qsizetype from = server.requests().size();
        source.trackEvent(Core::TrackEvent::Finished, track, 10);
        source.trackEvent(Core::TrackEvent::Skipped, track, 1);
        source.trackEvent(Core::TrackEvent::Started, track, 0);
        QVERIFY(QTest::qWaitFor([&] { return requestsTo(QStringLiteral("/play-audio"), from) > 0; }, 5'000));
        QTest::qWait(100);
        QCOMPARE(requestsTo(QStringLiteral("/play-audio"), from), 1);
        const Tests::MockRequest* report = server.last(QStringLiteral("/play-audio"));
        QCOMPARE(report->formValue(QStringLiteral("track-id")), QStringLiteral("5"));
        QCOMPARE(report->formValue(QStringLiteral("album-id")), QStringLiteral("7"));
    }

    void anotherQualityChangesTheFiles() {  // QUAL-05
        YandexSource::Source source(&library);
        Core::SourceRegistry registry;
        registry.add(YandexSource::kSourceName, &source);
        QSignalSpy changed(&registry, &Core::SourceRegistry::filesChanged);
        source.setQuality(Yandex::Quality::Standard);
        QCOMPARE(changed.count(), 0);
        source.setQuality(Yandex::Quality::Economy);
        QCOMPARE(source.quality(), Yandex::Quality::Economy);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().first().toString(), YandexSource::kSourceName);
    }

    void lyricsAreTheSupplementsPlainText() {  // LYR-02
        server.fixture(
            "GET", QStringLiteral("/tracks/33311009/supplement"), QStringLiteral("tracks-supplement/with-lyrics")
        );
        YandexSource::Source source(&library);
        const qsizetype from = server.requests().size();
        bool answeredInside = true;
        const Core::LyricsAnswer answer = AskLyrics(
            source, YandexSource::TrackOf(Sample(QStringLiteral("33311009"), QStringLiteral("4"))), &answeredInside
        );
        QVERIFY(!answeredInside);
        QCOMPARE(answer.status, Core::LyricsAnswer::Status::Found);
        QCOMPARE(answer.from, Core::LyricsFrom::Catalog);
        QCOMPARE(answer.lyrics.kind, Core::LyricsKind::Plain);
        const QString expected =
            Tests::ExpectedObject(QStringLiteral("tracks-supplement/with-lyrics"))
                .value(QStringLiteral("lyrics"))
                .toString();
        QCOMPARE(answer.lyrics, *Core::PlainLyrics(expected));
        QCOMPARE(requestsTo(QStringLiteral("/tracks/"), from), 1);
    }

    void noSupplementTextOrABadRequestIsMissingAndAServerErrorIsAFailure() {  // LYR-02, LYR-17
        server.fixture(
            "GET", QStringLiteral("/tracks/51/supplement"), QStringLiteral("tracks-supplement/without-lyrics")
        );
        server.json("GET", QStringLiteral("/tracks/52/supplement"), "{}", 500);
        server.fixture(
            "GET", QStringLiteral("/tracks/53/supplement"), QStringLiteral("tracks-supplement/400-validate")
        );
        YandexSource::Source source(&library);
        QCOMPARE(
            AskLyrics(source, YandexSource::TrackOf(Sample(QStringLiteral("51"), {}))).status,
            Core::LyricsAnswer::Status::Missing
        );
        QCOMPARE(
            AskLyrics(source, YandexSource::TrackOf(Sample(QStringLiteral("52"), {}))).status,
            Core::LyricsAnswer::Status::Failed
        );
        QCOMPARE(
            AskLyrics(source, YandexSource::TrackOf(Sample(QStringLiteral("53"), {}))).status,
            Core::LyricsAnswer::Status::Missing
        );
    }

    void withoutALoginYandexIsNotAskedForLyrics() {  // LYR-16
        Tests::LocalNetworkAccessManager network;
        Yandex::ApiClient anonymous(&network);
        anonymous.setBaseUrl(server.baseUrl());
        Yandex::Library nobody(&anonymous);
        YandexSource::Source source(&nobody);
        const qsizetype from = server.requests().size();
        bool answeredInside = true;
        const Core::LyricsAnswer answer =
            AskLyrics(source, YandexSource::TrackOf(Sample(QStringLiteral("33311009"), {})), &answeredInside);
        QVERIFY(!answeredInside);
        QCOMPARE(answer.status, Core::LyricsAnswer::Status::NoAccount);
        QTest::qWait(50);
        QCOMPARE(server.requests().size(), from);
    }

    void aTrackOfNoSourceFailsAfterOpenReturns() {
        Core::SourceRegistry registry;
        Core::Track unknown;
        unknown.key = {QStringLiteral("nowhere"), QStringLiteral("1")};
        std::optional<Core::TOpenResult> result;
        registry.open(unknown, [&](Core::TOpenResult opened) { result = std::move(opened); });
        QVERIFY(!result);
        QVERIFY(QTest::qWaitFor([&] { return result.has_value(); }, 1'000));
        QVERIFY(std::holds_alternative<Core::TrackFailure>(*result));
        QCOMPARE(std::get<Core::TrackFailure>(*result).kind, Core::FailureKind::Track);
    }

    void theDisplayTitleNamesTheArtistsFirst() {
        const QJsonValue live = ResultOf(QStringLiteral("tracks/version-and-artists")).toArray().first();
        const Core::Track track = YandexSource::TrackOf(Yandex::ApiClient::ParseTrack(live));
        QCOMPARE(track.displayTitle(), QStringLiteral("A, B - Song (Live)"));
        Core::Track untitled;
        untitled.title = QStringLiteral("Song");
        QCOMPARE(untitled.displayTitle(), QStringLiteral("Song"));
    }
};

QTEST_GUILESS_MAIN(TestYandexSource)
#include "yandex_source_test.moc"
