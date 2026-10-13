#include "core/track.h"
#include "core/track_source.h"
#include "sources/yandex/source.h"
#include "sources/yandex/tracks.h"
#include "support/spec_fixtures.h"
#include "support/yandex_json.h"
#include "yandex/api_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>
#include <QTest>
#include <QUrl>

#include <optional>

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

}  // namespace

class TestYandexSource : public QObject {
    Q_OBJECT
private Q_SLOTS:
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
        const YandexSource::Source source;
        QVERIFY(source.coverUrl(local, 400).isEmpty());
        QVERIFY(source.webPage(local).isEmpty());
    }

    void coverAndPageAreTheClientsOwn() {
        YandexSource::Source source;
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
