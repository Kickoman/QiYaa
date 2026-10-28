#include "audio/audio_engine.h"
#include "core/player.h"
#include "core/search.h"
#include "core/track_source.h"
#include "sources/spotify/autoplay.h"
#include "sources/spotify/catalog.h"
#include "sources/spotify/library.h"
#include "sources/spotify/source.h"
#include "sources/spotify/tracks.h"
#include "spotify/protocol/ids.h"
#include "spotify/protocol/protobuf.h"
#include "support/spotify_stack.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QUrlQuery>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using SpotifyProtocol::ProtoReader;
using SpotifyProtocol::ProtoWriter;
using SpotifyProtocol::TBytes;
using Tests::BytesOf;
using Tests::MockRequest;
using Tests::MockResponse;

QString Uri(const char* kind, std::uint8_t mark) {
    SpotifyProtocol::TGid gid{};
    gid.fill(mark);
    return QStringLiteral("spotify:%1:%2")
        .arg(QLatin1String(kind), QString::fromStdString(SpotifyProtocol::Base62FromGid(gid)));
}

QString TrackUri(std::uint8_t mark) {
    return Uri("track", mark);
}

ProtoWriter Image(std::uint8_t mark, int size, int width) {
    ProtoWriter image;
    image.bytes(1, TBytes(20, mark)).varint(2, static_cast<std::uint64_t>(size));
    if (width > 0) {
        image.varint(3, static_cast<std::uint64_t>(width * 2));
    }
    return image;
}

TBytes Track(std::uint8_t mark, const std::string& name, bool files, std::optional<std::uint8_t> alternative = {}) {
    ProtoWriter artist;
    artist.bytes(1, TBytes(16, 0xa0)).string(2, "Band");
    ProtoWriter date;
    date.varint(1, 1991 * 2);
    ProtoWriter covers;
    covers.message(1, Image(0x64, 1, 64)).message(1, Image(0x30, 0, 300)).message(1, Image(0x40, 2, 0));
    ProtoWriter album;
    album.bytes(1, TBytes(16, 0xab)).string(2, "Album").message(6, date).message(17, covers);
    ProtoWriter track;
    track.bytes(1, TBytes(16, mark)).string(2, name).message(3, album).message(4, artist).varint(7, 200000 * 2);
    if (files) {
        for (const std::uint64_t format : {0, 1, 2}) {
            ProtoWriter file;
            file.bytes(1, TBytes(20, static_cast<std::uint8_t>(mark + format))).varint(2, format);
            track.message(12, file);
        }
    }
    if (alternative) {
        ProtoWriter relinked;
        ProtoWriter file;
        file.bytes(1, TBytes(20, *alternative)).varint(2, 2);
        relinked.bytes(1, TBytes(16, *alternative)).message(12, file);
        track.message(13, relinked);
    }
    return track.data();
}

TBytes Album(std::uint8_t mark, const std::string& name, const std::vector<std::vector<std::uint8_t>>& discs) {
    ProtoWriter album;
    album.bytes(1, TBytes(16, mark)).string(2, name);
    int number = 1;
    for (const auto& disc : discs) {
        ProtoWriter writer;
        writer.varint(1, static_cast<std::uint64_t>(number++ * 2));
        for (const std::uint8_t track : disc) {
            ProtoWriter entry;
            entry.bytes(1, TBytes(16, track));
            writer.message(3, entry);
        }
        album.message(11, writer);
    }
    return album.data();
}

TBytes
Artist(std::uint8_t mark, const std::string& name, const std::vector<std::pair<std::string, std::uint8_t>>& tops) {
    ProtoWriter artist;
    artist.bytes(1, TBytes(16, mark)).string(2, name);
    for (const auto& [country, track] : tops) {
        ProtoWriter entry;
        ProtoWriter gid;
        gid.bytes(1, TBytes(16, track));
        entry.string(1, country).message(2, gid);
        artist.message(4, entry);
    }
    return artist.data();
}

QStringList AskedUris(const QByteArray& body, std::uint64_t* kind = nullptr) {
    QStringList uris;
    const TBytes bytes(body.cbegin(), body.cend());
    ProtoReader request(bytes);
    while (request.next()) {
        ProtoReader entity(request.bytes());
        while (entity.next()) {
            if (entity.field() == 1) {
                uris << QString::fromStdString(entity.string());
            } else if (kind) {
                ProtoReader query(entity.bytes());
                if (query.next()) {
                    *kind = query.varint();
                }
            }
        }
    }
    return uris;
}

QString ReadString(const QByteArray& body, int field) {
    const TBytes bytes(body.cbegin(), body.cend());
    ProtoReader reader(bytes);
    while (reader.next()) {
        if (reader.field() == field) {
            return QString::fromStdString(reader.string());
        }
    }
    return {};
}

QStringList ReadStrings(const QByteArray& body, int field) {
    QStringList strings;
    const TBytes bytes(body.cbegin(), body.cend());
    ProtoReader reader(bytes);
    while (reader.next()) {
        if (reader.field() == field) {
            strings << QString::fromStdString(reader.string());
        }
    }
    return strings;
}

QByteArray JsonOf(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject ContextOf(const QStringList& uris, const QString& next) {
    QJsonArray tracks;
    for (const QString& uri : uris) {
        tracks << QJsonObject{{QStringLiteral("uri"), uri}, {QStringLiteral("uid"), QStringLiteral("u")}};
    }
    const QJsonObject page{{QStringLiteral("tracks"), tracks}, {QStringLiteral("next_page_url"), next}};
    return QJsonObject{
        {QStringLiteral("uri"), QStringLiteral("spotify:station:x")}, {QStringLiteral("pages"), QJsonArray{page}}
    };
}

struct Stack : Tests::SpotifyStack {
    QHash<QString, TBytes> entities;
    QList<QStringList> metadataAsked;
    QHash<QString, QList<QStringList>> collectionPages;
    Audio::AudioEngine engine;
    Core::SourceRegistry registry;
    Core::Player player{&engine, &registry};
    SpotifySource::Library library{&web, &session};
    SpotifySource::Source source{&web, &session, &network};
    SpotifySource::Catalog catalog{&player, &library};
    QList<Core::SourceNotice> notices;

    Stack() {
        registry.add(SpotifySource::kSourceName, &source);
        QObject::connect(&catalog, &SpotifySource::Catalog::noticed, [this](const Core::SourceNotice& notice) {
            notices << notice;
        });
        http.on("POST", QStringLiteral("/extended-metadata/v0/extended-metadata"), [this](const MockRequest& request) {
            const QStringList uris = AskedUris(request.body);
            metadataAsked << uris;
            ProtoWriter array;
            for (auto it = uris.crbegin(); it != uris.crend(); ++it) {
                ProtoWriter header;
                ProtoWriter data;
                const bool known = entities.contains(*it);
                header.varint(1, known ? 200 : 404);
                data.message(1, header).string(2, it->toStdString());
                if (known) {
                    ProtoWriter any;
                    any.string(1, "type.googleapis.com/x").bytes(2, entities.value(*it));
                    data.message(3, any);
                }
                array.message(3, data);
            }
            ProtoWriter reply;
            reply.message(2, array);
            return MockResponse{200, BytesOf(reply.data())};
        });
        http.on("POST", QStringLiteral("/collection/v2/paging"), [this](const MockRequest& request) {
            const QString set = ReadString(request.body, 2);
            const QString token = ReadString(request.body, 3);
            const QList<QStringList> pages = collectionPages.value(set);
            const int index = token.isEmpty() ? 0 : token.toInt();
            if (pages.isEmpty() && set == QLatin1String("albums")) {
                return MockResponse{403, {}};
            }
            ProtoWriter page;
            for (const QString& uri : pages.value(index)) {
                ProtoWriter item;
                item.string(1, uri.toStdString()).varint(2, 1000);
                page.message(1, item);
            }
            if (index + 1 < pages.size()) {
                page.string(2, std::to_string(index + 1));
            }
            return MockResponse{200, BytesOf(page.data())};
        });
    }

    void add(const QString& uri, const TBytes& payload) { entities.insert(uri, payload); }

    template <typename T>
    std::optional<T> wait(std::function<void(SpotifySource::Library::TDone<T>)> call, QString* error = nullptr) {
        std::optional<T> result;
        call([&](const T& value, const QString& text) {
            result = value;
            if (error) {
                *error = text;
            }
        });
        if (!QTest::qWaitFor([&] { return result.has_value(); }, 5000)) {
            return std::nullopt;
        }
        return result;
    }

    QStringList titlesOf(const QList<Core::Track>& tracks) const {
        QStringList titles;
        for (const Core::Track& track : tracks) {
            titles << track.title + (track.available ? QString() : QStringLiteral(" (unavailable)"));
        }
        return titles;
    }

    QStringList queueTitles() const {
        QStringList titles;
        for (const Core::QueueEntry& entry : player.entries()) {
            titles << entry.track.title;
        }
        return titles;
    }
};

}  // namespace

class TestSpotifySource : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void likedSongsAreEveryPageInSpotifysOrderWithTheirAvailability() {  // SPOT-07, SPOT-15
        Stack stack;
        QVERIFY(stack.loginStored());
        stack.collectionPages.insert(
            QStringLiteral("collection"), {{TrackUri(1), Uri("album", 9), TrackUri(2)}, {TrackUri(3), TrackUri(4)}}
        );
        stack.add(TrackUri(1), Track(1, "Plays", true));
        stack.add(TrackUri(2), Track(2, "Relinked", false, 0x22));
        stack.add(TrackUri(3), Track(3, "Gone", false));
        const auto tracks = stack.wait<QList<Core::Track>>([&](auto done) { stack.library.likedTracks(done); });
        QVERIFY(tracks);
        QCOMPARE(stack.titlesOf(*tracks), (QStringList{"Plays", "Relinked", "Gone (unavailable)"}));
        const Core::Track& first = tracks->first();
        QCOMPARE(first.key.source, SpotifySource::kSourceName);
        QCOMPARE(SpotifySource::UriOf(first.key), TrackUri(1));
        QCOMPARE(first.artists, QStringList{QStringLiteral("Band")});
        QCOMPARE(first.album, QStringLiteral("Album"));
        QCOMPARE(first.year, 1991);
        QCOMPARE(first.durationMs, qint64{200000});
        QCOMPARE(stack.metadataAsked.size(), 1);
        QCOMPARE(stack.metadataAsked.first(), (QStringList{TrackUri(1), TrackUri(2), TrackUri(3), TrackUri(4)}));
    }

    void metadataIsAskedAHundredAtATimeAndMatchedByUri() {
        Stack stack;
        QVERIFY(stack.loginStored());
        QStringList uris;
        for (int i = 1; i <= 150; ++i) {
            const auto mark = static_cast<std::uint8_t>(i);
            uris << TrackUri(mark);
            stack.add(TrackUri(mark), Track(mark, std::to_string(i), true));
        }
        const auto tracks = stack.wait<QList<Core::Track>>([&](auto done) { stack.library.tracks(uris, done); });
        QVERIFY(tracks);
        QCOMPARE(tracks->size(), 150);
        QCOMPARE(tracks->at(0).title, QStringLiteral("1"));
        QCOMPARE(tracks->at(149).title, QStringLiteral("150"));
        QCOMPARE(stack.metadataAsked.size(), 2);
        const qsizetype first = stack.metadataAsked[0].size();
        const qsizetype second = stack.metadataAsked[1].size();
        QCOMPARE(std::max(first, second), qsizetype{100});
        QCOMPARE(std::min(first, second), qsizetype{50});
    }

    void coversAreTheSmallestImageAtLeastAsLargeAsShownAndTheTrackHasItsPage() {  // SPOT-19
        const std::optional<SpotifyProtocol::TrackInfo> info = SpotifyProtocol::ParseTrack(Track(7, "Song", true));
        QVERIFY(info);
        const Core::Track track = SpotifySource::TrackOf(*info);
        const auto cover = [&track](int pixels) { return SpotifySource::CoverUrlOf(track, pixels).toString(); };
        QVERIFY(cover(50).startsWith(QStringLiteral("https://i.scdn.co/image/6464")));
        QVERIFY(cover(64).endsWith(QStringLiteral("6464646464646464646464646464646464646464")));
        QVERIFY(cover(200).endsWith(QStringLiteral("3030303030303030303030303030303030303030")));
        QVERIFY(cover(600).endsWith(QStringLiteral("4040404040404040404040404040404040404040")));
        QVERIFY(cover(2000).endsWith(QStringLiteral("4040404040404040404040404040404040404040")));
        QCOMPARE(
            SpotifySource::WebPageOf(track).toString(), QStringLiteral("https://open.spotify.com/track/") + track.key.id
        );
    }

    void playlistsAreTheRootlistsOwnAndTheyPlayTheirTracks() {  // SPOT-08
        Stack stack;
        QVERIFY(stack.loginStored());
        const auto list = [](const QList<std::pair<QString, QString>>& entries, int length) {
            ProtoWriter contents;
            contents.varint(1, 0);
            for (const auto& [uri, name] : entries) {
                ProtoWriter item;
                item.string(1, uri.toStdString());
                contents.message(3, item);
            }
            for (const auto& [uri, name] : entries) {
                ProtoWriter attributes;
                attributes.string(1, name.toStdString());
                ProtoWriter meta;
                meta.message(2, attributes);
                contents.message(4, meta);
            }
            ProtoWriter page;
            page.varint(2, static_cast<std::uint64_t>(length)).message(5, contents);
            return BytesOf(page.data());
        };
        const QByteArray rootlist = list(
            {{QStringLiteral("spotify:start-group:g:Folder"), QString()},
             {Uri("playlist", 0x51), QStringLiteral("Road")},
             {QStringLiteral("spotify:end-group:g"), QString()},
             {Uri("playlist", 0x52), QStringLiteral("Home")}},
            4
        );
        stack.http.on("GET", QStringLiteral("/playlist/v2/user/owner/rootlist"), [rootlist](const MockRequest&) {
            return MockResponse{200, rootlist};
        });
        const auto playlists = stack.wait<QList<SpotifySource::Item>>([&](auto done) { stack.library.playlists(done); }
        );
        QVERIFY(playlists);
        QCOMPARE(playlists->size(), 2);
        QCOMPARE(playlists->at(0).name, QStringLiteral("Road"));
        QCOMPARE(playlists->at(1).uri, Uri("playlist", 0x52));
        const MockRequest* asked = stack.http.last(QStringLiteral("/playlist/v2/user/owner/rootlist"));
        QCOMPARE(asked->query.queryItemValue(QStringLiteral("from")), QStringLiteral("0"));
        QVERIFY(asked->query.queryItemValue(QStringLiteral("decorate")).contains(QStringLiteral("attributes")));

        const QByteArray road =
            list({{TrackUri(1), {}}, {QStringLiteral("spotify:episode:e"), {}}, {TrackUri(2), {}}}, 3);
        const QString path = QStringLiteral("/playlist/v2/playlist/") + SpotifySource::IdOfUri(Uri("playlist", 0x51));
        stack.http.on("GET", path, [road](const MockRequest&) { return MockResponse{200, road}; });
        stack.add(TrackUri(1), Track(1, "One", true));
        stack.add(TrackUri(2), Track(2, "Two", true));
        stack.catalog.playPlaylist(playlists->first());
        QTRY_COMPARE(stack.player.queueTitle(), QStringLiteral("Road"));
        QCOMPARE(stack.queueTitles(), (QStringList{"One", "Two"}));
    }

    void savedAlbumsAndFollowedArtistsAreNamedAndPlay() {  // SPOT-09
        Stack stack;
        QVERIFY(stack.loginStored());
        stack.collectionPages.insert(QStringLiteral("collection"), {{TrackUri(1), Uri("album", 0x71)}});
        stack.collectionPages.insert(QStringLiteral("artist"), {{Uri("artist", 0x81)}});
        stack.add(Uri("album", 0x71), Album(0x71, "Double", {{3, 4}, {5}}));
        stack.add(Uri("artist", 0x81), Artist(0x81, "Band", {{"ZZ", 6}, {"BY", 7}}));
        for (const std::uint8_t mark : {3, 4, 5, 6, 7}) {
            stack.add(TrackUri(mark), Track(mark, std::to_string(mark), true));
        }
        const auto albums = stack.wait<QList<SpotifySource::Item>>([&](auto done) { stack.library.savedAlbums(done); });
        QVERIFY(albums);
        QCOMPARE(albums->size(), 1);
        QCOMPARE(albums->first().name, QStringLiteral("Double"));
        const auto artists = stack.wait<QList<SpotifySource::Item>>([&](auto done) {
            stack.library.followedArtists(done);
        });
        QVERIFY(artists);
        QCOMPARE(artists->first().name, QStringLiteral("Band"));

        stack.catalog.playAlbum(albums->first());
        QTRY_COMPARE(stack.player.queueTitle(), QStringLiteral("Double"));
        QCOMPARE(stack.queueTitles(), (QStringList{"3", "4", "5"}));
        QTRY_VERIFY(!stack.session.country().isEmpty());
        stack.catalog.playArtist(artists->first());
        QTRY_COMPARE(stack.player.queueTitle(), QStringLiteral("Band"));
        QCOMPARE(stack.queueTitles(), (QStringList{"7"}));
    }

    void forYouIsTheHomesMadeForYouSection() {  // SPOT-10
        Stack stack;
        QVERIFY(stack.loginStored());
        const auto section = [](const QString& uri, const QList<std::pair<QString, QString>>& cards) {
            QJsonArray items;
            for (const auto& [type, name] : cards) {
                const QJsonObject data{
                    {QStringLiteral("__typename"), type},
                    {QStringLiteral("uri"), QStringLiteral("spotify:playlist:") + name},
                    {QStringLiteral("name"), name},
                };
                items << QJsonObject{{QStringLiteral("content"), QJsonObject{{QStringLiteral("data"), data}}}};
            }
            return QJsonObject{
                {QStringLiteral("uri"), uri},
                {QStringLiteral("sectionItems"), QJsonObject{{QStringLiteral("items"), items}}},
            };
        };
        const QJsonArray sections{
            section(QStringLiteral("spotify:section:other"), {{QStringLiteral("Playlist"), QStringLiteral("Chill")}}),
            section(
                QStringLiteral("spotify:section:0JQ5DAUnp4wcj0bCb3wh3S"),
                {{QStringLiteral("Playlist"), QStringLiteral("Mix1")},
                 {QStringLiteral("NotFound"), QStringLiteral("DJ")},
                 {QStringLiteral("Playlist"), QStringLiteral("Weekly")}}
            ),
        };
        const QJsonObject home{
            {QStringLiteral("sectionContainer"),
             QJsonObject{{QStringLiteral("sections"), QJsonObject{{QStringLiteral("items"), sections}}}}},
        };
        const QByteArray reply =
            JsonOf(QJsonObject{{QStringLiteral("data"), QJsonObject{{QStringLiteral("home"), home}}}});
        stack.http.json("POST", QStringLiteral("/pathfinder/v2/query"), reply);
        const auto items = stack.wait<QList<SpotifySource::Item>>([&](auto done) { stack.library.forYou(done); });
        QVERIFY(items);
        QCOMPARE(items->size(), 2);
        QCOMPARE(items->at(0).name, QStringLiteral("Mix1"));
        QCOMPARE(items->at(1).name, QStringLiteral("Weekly"));
        const QJsonObject asked =
            QJsonDocument::fromJson(stack.http.last(QStringLiteral("/pathfinder/v2/query"))->body).object();
        QCOMPARE(asked.value(QStringLiteral("operationName")).toString(), QStringLiteral("home"));
    }

    void searchPlaysTheBestArtistOrTheFoundTracksAndTriesTheNextHash() {  // SPOT-11
        Stack stack;
        QVERIFY(stack.loginStored());
        stack.add(Uri("artist", 0x81), Artist(0x81, "Band", {{"BY", 1}}));
        stack.add(TrackUri(1), Track(1, "Hit", true));
        stack.add(TrackUri(2), Track(2, "Found", true));
        QJsonObject best{
            {QStringLiteral("__typename"), QStringLiteral("Artist")},
            {QStringLiteral("uri"), Uri("artist", 0x81)},
            {QStringLiteral("profile"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Band")}}},
        };
        int asked = 0;
        stack.http.on("POST", QStringLiteral("/pathfinder/v2/query"), [&](const MockRequest&) {
            if (++asked == 1) {
                return MockResponse{412, {}};
            }
            const QJsonObject top{
                {QStringLiteral("itemsV2"),
                 QJsonArray{QJsonObject{{QStringLiteral("item"), QJsonObject{{QStringLiteral("data"), best}}}}}},
            };
            const QJsonObject tracks{
                {QStringLiteral("items"),
                 QJsonArray{QJsonObject{
                     {QStringLiteral("item"),
                      QJsonObject{{QStringLiteral("data"), QJsonObject{{QStringLiteral("uri"), TrackUri(2)}}}}}
                 }}},
            };
            const QJsonObject search{{QStringLiteral("topResultsV2"), top}, {QStringLiteral("tracksV2"), tracks}};
            return MockResponse{
                200, JsonOf(QJsonObject{{QStringLiteral("data"), QJsonObject{{QStringLiteral("searchV2"), search}}}})
            };
        });
        QTRY_VERIFY(!stack.session.country().isEmpty());
        stack.catalog.search(QStringLiteral("band"));
        QTRY_COMPARE(stack.player.queueTitle(), QStringLiteral("Band"));
        QCOMPARE(stack.queueTitles(), QStringList{QStringLiteral("Hit")});
        QCOMPARE(asked, 2);

        best =
            QJsonObject{{QStringLiteral("__typename"), QStringLiteral("Track")}, {QStringLiteral("uri"), TrackUri(2)}};
        stack.catalog.search(QStringLiteral("found"));
        QTRY_COMPARE(stack.player.queueTitle(), QStringLiteral("Search: found"));
        QCOMPARE(stack.queueTitles(), QStringList{QStringLiteral("Found")});
    }

    void searchShowsTheBestTrackOnceWithArtistsAndAlbums() {  // SPOT-11, SRC-13, ADD-05
        Stack stack;
        QVERIFY(stack.loginStored());
        stack.add(TrackUri(1), Track(1, "Hit", true));
        stack.add(TrackUri(2), Track(2, "Found", true));
        stack.http.on("POST", QStringLiteral("/pathfinder/v2/query"), [](const MockRequest&) {
            const auto wrapped = [](const QJsonObject& data) {
                return QJsonObject{{QStringLiteral("item"), QJsonObject{{QStringLiteral("data"), data}}}};
            };
            const QJsonObject best{
                {QStringLiteral("__typename"), QStringLiteral("Track")}, {QStringLiteral("uri"), TrackUri(2)}
            };
            const QJsonObject band{{QStringLiteral("name"), QStringLiteral("Band")}};
            const QJsonObject artist{
                {QStringLiteral("__typename"), QStringLiteral("Artist")},
                {QStringLiteral("uri"), Uri("artist", 0x81)},
                {QStringLiteral("profile"), band},
            };
            const QJsonObject album{
                {QStringLiteral("__typename"), QStringLiteral("Album")},
                {QStringLiteral("uri"), Uri("album", 0x71)},
                {QStringLiteral("name"), QStringLiteral("Double")},
                {QStringLiteral("type"), QStringLiteral("SINGLE")},
                {QStringLiteral("date"), QJsonObject{{QStringLiteral("year"), 1991}}},
                {QStringLiteral("artists"),
                 QJsonObject{{QStringLiteral("items"), QJsonArray{QJsonObject{{QStringLiteral("profile"), band}}}}}},
            };
            const QJsonObject search{
                {QStringLiteral("topResultsV2"), QJsonObject{{QStringLiteral("itemsV2"), QJsonArray{wrapped(best)}}}},
                {QStringLiteral("tracksV2"),
                 QJsonObject{
                     {QStringLiteral("items"),
                      QJsonArray{
                          wrapped(QJsonObject{{QStringLiteral("uri"), TrackUri(1)}}),
                          wrapped(QJsonObject{{QStringLiteral("uri"), TrackUri(2)}})
                      }}
                 }},
                {QStringLiteral("artists"),
                 QJsonObject{{QStringLiteral("items"), QJsonArray{QJsonObject{{QStringLiteral("data"), artist}}}}}},
                {QStringLiteral("albumsV2"),
                 QJsonObject{{QStringLiteral("items"), QJsonArray{QJsonObject{{QStringLiteral("data"), album}}}}}},
            };
            return MockResponse{
                200, JsonOf(QJsonObject{{QStringLiteral("data"), QJsonObject{{QStringLiteral("searchV2"), search}}}})
            };
        });
        QTRY_VERIFY(!stack.session.country().isEmpty());
        std::optional<Core::SearchResults> results;
        stack.catalog.find(QStringLiteral("found"), [&](const Core::SearchResults& found, const QString& error) {
            QVERIFY(error.isEmpty());
            results = found;
        });
        QTRY_VERIFY(results.has_value());
        QCOMPARE(results->bestTrack->title, QStringLiteral("Found"));
        QCOMPARE(results->tracks.size(), 1);
        QCOMPARE(results->tracks.front().title, QStringLiteral("Hit"));
        QCOMPARE(results->artists.size(), 1);
        QCOMPARE(results->artists.front().id, Uri("artist", 0x81));
        QCOMPARE(results->albums.size(), 1);
        QCOMPARE(results->albums.front().artists, QStringList{"Band"});
        QCOMPARE(results->albums.front().year, 1991);
        QVERIFY(results->albums.front().single);
        QVERIFY(stack.player.entries().isEmpty());
        stack.catalog.add(QStringLiteral("Search: found"), results->playable(), Core::AddPlacement::End);
        QCOMPARE(stack.player.queueTitle(), QStringLiteral("Search: found"));
        QCOMPARE(stack.queueTitles(), (QStringList{"Found", "Hit"}));
        QCOMPARE(stack.player.currentIndex(), 0);
        QCOMPARE(stack.player.state(), Core::Player::State::Stopped);
    }

    void aPickGoesOnWithAutoplayFromItsOwnUriAndRecentTracks() {  // SPOT-20
        Stack stack;
        QVERIFY(stack.loginStored());
        stack.collectionPages.insert(QStringLiteral("collection"), {{TrackUri(1), TrackUri(2)}});
        for (const std::uint8_t mark : {1, 2, 11, 12, 13}) {
            stack.add(TrackUri(mark), Track(mark, std::to_string(mark), true));
        }
        stack.http.on("POST", QStringLiteral("/context-resolve/v1/autoplay"), [](const MockRequest&) {
            return MockResponse{
                200,
                JsonOf(ContextOf(
                    {TrackUri(11), TrackUri(12)},
                    QStringLiteral("hm://radio-apollo/v5/tracks/spotify:station:x?salt=1&prev_tracks=a,b")
                ))
            };
        });
        stack.player.setContinueWithVibeByDefault(true);
        stack.catalog.playLikes();
        QTRY_COMPARE(stack.player.queueTitle(), QStringLiteral("Liked Songs"));
        QVERIFY(stack.player.continuesWithVibe());
        QVERIFY(stack.player.canSwitchVibe());
        QTRY_VERIFY(stack.http.last(QStringLiteral("/context-resolve/v1/autoplay")));
        const MockRequest* autoplay = stack.http.last(QStringLiteral("/context-resolve/v1/autoplay"));
        QCOMPARE(ReadString(autoplay->body, 1), QStringLiteral("spotify:user:owner:collection"));
        QCOMPARE(autoplay->headers.value("accept"), QByteArray("application/json"));
        QVERIFY(autoplay->query.isEmpty());
        QTRY_COMPARE(stack.queueTitles(), (QStringList{"1", "2", "11", "12"}));

        const Core::Track a = stack.player.entries()[0].track;
        Core::Track local = a;
        local.key.source = QStringLiteral("local");
        const Core::Track b = stack.player.entries()[1].track;
        const auto pages = SpotifySource::Autoplay::Of(&stack.library, QStringLiteral("spotify:album:x"));
        std::optional<Core::ContinuationReply> reply;
        QVERIFY(pages->more(Core::ContinuationRequest{.history = {b, local, a}}, [&](const Core::ContinuationReply& r) {
            reply = r;
        }));
        QTRY_VERIFY(reply);
        QCOMPARE(
            ReadStrings(stack.http.last(QStringLiteral("/context-resolve/v1/autoplay"))->body, 2),
            (QStringList{TrackUri(1), TrackUri(2)})
        );
        stack.http.on("GET", QStringLiteral("/radio-apollo/v5/tracks/spotify:station:x"), [](const MockRequest&) {
            QJsonArray tracks{QJsonObject{{QStringLiteral("uri"), TrackUri(13)}}};
            return MockResponse{200, JsonOf(QJsonObject{{QStringLiteral("tracks"), tracks}})};
        });
        reply.reset();
        QVERIFY(pages->more(Core::ContinuationRequest{}, [&](const Core::ContinuationReply& r) { reply = r; }));
        QTRY_VERIFY(reply);
        QCOMPARE(reply->tracks.size(), 1);
        QCOMPARE(reply->tracks.first().title, QStringLiteral("13"));
        const MockRequest* page = stack.http.last(QStringLiteral("/radio-apollo/v5/tracks/spotify:station:x"));
        QCOMPARE(page->query.queryItemValue(QStringLiteral("prev_tracks")), QStringLiteral("a,b"));
        QVERIFY(!page->query.hasQueryItem(QStringLiteral("country")));
    }

    void aFailedAutoplayIsAFailureOfTheContinuation() {  // SPOT-21
        Stack stack;
        QVERIFY(stack.loginStored());
        stack.http.json("POST", QStringLiteral("/context-resolve/v1/autoplay"), QByteArray(), 500);
        const auto autoplay = SpotifySource::Autoplay::Of(&stack.library, QStringLiteral("spotify:album:x"));
        std::optional<Core::ContinuationReply> reply;
        QVERIFY(autoplay->more(Core::ContinuationRequest{}, [&](const Core::ContinuationReply& r) { reply = r; }));
        QTRY_VERIFY(reply);
        QVERIFY(reply->tracks.isEmpty());
        QVERIFY(!reply->failure.isEmpty());
    }

    void radioIsTheTracksStationWithItsOwnPages() {  // SPOT-12
        Stack stack;
        QVERIFY(stack.loginStored());
        for (const std::uint8_t mark : {1, 21, 22}) {
            stack.add(TrackUri(mark), Track(mark, "R" + std::to_string(mark), true));
        }
        const QString station =
            QStringLiteral("/context-resolve/v1/spotify:station:track:") + SpotifySource::IdOfUri(TrackUri(1));
        stack.http.json("GET", station, JsonOf(ContextOf({TrackUri(21), TrackUri(22)}, QString())));
        const std::optional<SpotifyProtocol::TrackInfo> seed = SpotifyProtocol::ParseTrack(Track(1, "Seed", true));
        stack.catalog.playRadio(SpotifySource::TrackOf(*seed));
        QTRY_COMPARE(stack.player.queueTitle(), QStringLiteral("Seed: radio"));
        QCOMPARE(stack.queueTitles().mid(0, 2), (QStringList{"R21", "R22"}));
        QVERIFY(stack.player.hasOwnContinuation());
        QVERIFY(!stack.player.canSwitchVibe());
        QVERIFY(stack.http.last(station)->query.isEmpty());
    }

    void aTrackOpensInTheQualityInEffectAndNeedsTheAccount() {  // SPOT-13, SPOT-06
        Stack stack;
        const std::optional<SpotifyProtocol::TrackInfo> info = SpotifyProtocol::ParseTrack(Track(1, "Song", true));
        const Core::Track track = SpotifySource::TrackOf(*info);
        const auto open = [&stack, &track] {
            std::optional<Core::TOpenResult> result;
            stack.source.open(track, [&result](Core::TOpenResult opened) { result = std::move(opened); });
            if (!QTest::qWaitFor([&result] { return result.has_value(); }, 5000)) {
                return std::optional<Core::TOpenResult>();
            }
            return result;
        };
        std::optional<Core::TOpenResult> refused = open();
        QVERIFY(refused);
        QCOMPARE(std::get<Core::TrackFailure>(*refused).kind, Core::FailureKind::Auth);

        QVERIFY(stack.loginStored());
        stack.add(TrackUri(1), Track(1, "Song", true));
        stack.http.onPrefix("GET", QStringLiteral("/storage-resolve/"), [&stack](const MockRequest&) {
            ProtoWriter storage;
            storage.varint(1, 0).string(2, (stack.http.baseUrl() + QStringLiteral("/cdn/file")).toStdString());
            return MockResponse{200, BytesOf(storage.data())};
        });
        std::optional<Core::TOpenResult> standard = open();
        QVERIFY(standard);
        QVERIFY(std::holds_alternative<Core::OpenedTrack>(*standard));
        QCOMPARE(std::get<Core::OpenedTrack>(*standard).bitrateKbps, 320);
        stack.source.setEconomy(true);
        std::optional<Core::TOpenResult> economy = open();
        QVERIFY(economy);
        QCOMPARE(std::get<Core::OpenedTrack>(*economy).bitrateKbps, 96);
    }

    void aRefusedKeyIsAnAccountFailure() {  // SPOT-17
        Stack stack;
        stack.accessPoint.keyRefusal = [](int) { return std::optional<std::uint16_t>(1); };
        QVERIFY(stack.loginStored());
        stack.add(TrackUri(1), Track(1, "Song", true));
        const std::optional<SpotifyProtocol::TrackInfo> info = SpotifyProtocol::ParseTrack(Track(1, "Song", true));
        std::optional<Core::TOpenResult> result;
        stack.source.open(SpotifySource::TrackOf(*info), [&result](Core::TOpenResult opened) {
            result = std::move(opened);
        });
        QTRY_VERIFY(result);
        QCOMPARE(std::get<Core::TrackFailure>(*result).kind, Core::FailureKind::Auth);
    }
};

QTEST_MAIN(TestSpotifySource)
#include "spotify_source_test.moc"
