#include "sources/spotify/library.h"

#include "sources/spotify/tracks.h"
#include "spotify/client/session.h"
#include "spotify/client/web.h"
#include "spotify/protocol/catalog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QPointer>
#include <QTimeZone>
#include <QUrl>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace SpotifySource {

using SpotifyClient::WebReply;
using SpotifyClient::WebRequest;
using SpotifyProtocol::ExtensionKind;

namespace {

const QByteArray kProtobuf = "application/x-protobuf";
const QByteArray kJson = "application/json";
const QString kTrackPrefix = QStringLiteral("spotify:track:");
const QString kAlbumPrefix = QStringLiteral("spotify:album:");
const QString kArtistPrefix = QStringLiteral("spotify:artist:");
const QString kMadeForYou = QStringLiteral("spotify:section:0JQ5DAUnp4wcj0bCb3wh3S");
const QString kPagePrefix = QStringLiteral("hm://");
const QString kRootlistDecoration = QStringLiteral("revision,attributes,length,owner,capabilities,status_code");
const QStringList kHomeHashes{
    QStringLiteral("76243c78b0e20ecdbe41b794dec8cbe73f75e585b0a7201b8d2e84578412847a"),
};
const QStringList kSearchHashes{
    QStringLiteral("eef7cc54888d91bdd6802623477873caa3948ae173a0c34fd86827b267e94c03"),
    QStringLiteral("fcad5a3e0d5af727fb76966f06971c19cfa2275e6ff7671196753e008611873c"),
    QStringLiteral("3c9d3f60dac5dea3876b6db3f534192b1c1d90032c4233c1bbaba526db41eb31"),
    QStringLiteral("4801118d4a100f756e833d33984436a3899cff359c532f8fd3aaf174b60b3b49"),
};

QByteArray Bytes(const SpotifyProtocol::TBytes& bytes) {
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
}

SpotifyProtocol::ByteView View(const QByteArray& bytes) {
    return {reinterpret_cast<const std::uint8_t*>(bytes.constData()), static_cast<std::size_t>(bytes.size())};
}

std::vector<std::string> StdStrings(const QStringList& texts) {
    std::vector<std::string> strings;
    strings.reserve(static_cast<std::size_t>(texts.size()));
    for (const QString& text : texts) {
        strings.push_back(text.toStdString());
    }
    return strings;
}

QString ErrorOf(const WebReply& reply) {
    return reply.error.isEmpty() ? Library::tr("Spotify: HTTP %1").arg(reply.status) : reply.error;
}

QString Unreadable() {
    return Library::tr("Spotify sent an answer QiYaa cannot read");
}

QStringList WithPrefix(const QStringList& uris, const QString& prefix) {
    QStringList kept;
    std::copy_if(uris.cbegin(), uris.cend(), std::back_inserter(kept), [&prefix](const QString& uri) {
        return uri.startsWith(prefix);
    });
    return kept;
}

QJsonObject DataOf(const QJsonObject& wrapper) {
    return wrapper.contains(QStringLiteral("data")) ? wrapper.value(QStringLiteral("data")).toObject() : wrapper;
}

QString TypeOf(const QJsonObject& object) {
    return object.value(QStringLiteral("__typename")).toString();
}

QJsonArray ItemsOf(const QJsonObject& object, const QString& list) {
    return object.value(list).toObject().value(QStringLiteral("items")).toArray();
}

std::optional<QList<Item>> MadeForYouOf(const QJsonObject& data) {
    const QJsonObject home = data.value(QStringLiteral("home")).toObject();
    if (!home.contains(QStringLiteral("sectionContainer"))) {
        return std::nullopt;
    }
    const QJsonObject container = home.value(QStringLiteral("sectionContainer")).toObject();
    QList<Item> playlists;
    for (const QJsonValue& value : ItemsOf(container, QStringLiteral("sections"))) {
        const QJsonObject section = value.toObject();
        if (section.value(QStringLiteral("uri")).toString() != kMadeForYou) {
            continue;
        }
        for (const QJsonValue& card : ItemsOf(section, QStringLiteral("sectionItems"))) {
            const QJsonObject playlist =
                card.toObject().value(QStringLiteral("content")).toObject().value(QStringLiteral("data")).toObject();
            if (TypeOf(playlist) == QLatin1String("Playlist")) {
                playlists << Item{
                    playlist.value(QStringLiteral("uri")).toString(),
                    playlist.value(QStringLiteral("name")).toString(),
                };
            }
        }
    }
    return playlists;
}

std::optional<Found> FoundOf(const QJsonObject& data) {
    if (!data.contains(QStringLiteral("searchV2"))) {
        return std::nullopt;
    }
    const QJsonObject search = data.value(QStringLiteral("searchV2")).toObject();
    Found found;
    for (const QJsonValue& value : ItemsOf(search, QStringLiteral("tracksV2"))) {
        const QJsonObject track = DataOf(value.toObject().value(QStringLiteral("item")).toObject());
        const QString uri = track.value(QStringLiteral("uri")).toString();
        if (uri.startsWith(kTrackPrefix)) {
            found.trackUris << uri;
        }
    }
    const QJsonArray top =
        search.value(QStringLiteral("topResultsV2")).toObject().value(QStringLiteral("itemsV2")).toArray();
    if (top.isEmpty()) {
        return found;
    }
    const QJsonObject best = DataOf(top.first().toObject().value(QStringLiteral("item")).toObject());
    const QString uri = best.value(QStringLiteral("uri")).toString();
    if (TypeOf(best) == QLatin1String("Artist") && uri.startsWith(kArtistPrefix)) {
        const QString name = best.value(QStringLiteral("profile")).toObject().value(QStringLiteral("name")).toString();
        found.kind = Found::Kind::Artist;
        found.best = Item{uri, name};
    } else if (TypeOf(best) == QLatin1String("Album") && uri.startsWith(kAlbumPrefix)) {
        found.kind = Found::Kind::Album;
        found.best = Item{uri, best.value(QStringLiteral("name")).toString()};
    }
    return found;
}

struct ContextUris {
    QStringList uris;
    QString next;
};

std::optional<ContextUris> ContextOf(const QByteArray& body) {
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject() || document.object().value(QStringLiteral("loading")).toBool()) {
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    const QJsonObject page = root.contains(QStringLiteral("pages"))
        ? root.value(QStringLiteral("pages")).toArray().first().toObject()
        : root;
    ContextUris context;
    for (const QJsonValue& value : page.value(QStringLiteral("tracks")).toArray()) {
        const QString uri = value.toObject().value(QStringLiteral("uri")).toString();
        if (uri.startsWith(kTrackPrefix)) {
            context.uris << uri;
        }
    }
    context.next = page.value(QStringLiteral("next_page_url")).toString();
    return context;
}

bool StaleQuery(const WebReply& reply, const QJsonObject& root) {
    if (reply.status == 400 || reply.status == 405 || reply.status == 412) {
        return true;
    }
    return reply.ok() && !root.value(QStringLiteral("data")).isObject();
}

}  // namespace

Library::Library(SpotifyClient::Web* spotifyWeb, SpotifyClient::Session* spotifySession, QObject* parent)
    : QObject(parent)
    , web(spotifyWeb)
    , session(spotifySession) { }

QString Library::username() const {
    return session->username();
}

QString Library::unreadable() {
    web->report(SpotifyClient::WebProblem::Content);
    return Unreadable();
}

void Library::metadata(const QStringList& uris, ExtensionKind kind, TDone<TEntities> done) {
    if (uris.isEmpty()) {
        done({}, {});
        return;
    }
    struct Batches {
        TEntities found;
        QString error;
        qsizetype waiting = 0;
    };
    auto state = std::make_shared<Batches>();
    QList<QStringList> chunks;
    for (qsizetype i = 0; i < uris.size(); i += kMetadataBatch) {
        chunks << uris.mid(i, kMetadataBatch);
    }
    state->waiting = chunks.size();
    QPointer<Library> self(this);
    for (const QStringList& chunk : chunks) {
        const WebRequest request{
            .method = "POST",
            .path = QStringLiteral("/extended-metadata/v0/extended-metadata"),
            .body = Bytes(SpotifyProtocol::MetadataRequest(StdStrings(chunk), kind)),
            .contentType = kProtobuf,
        };
        web->spclient(request, [self, state, done](const WebReply& reply) {
            if (!self) {
                return;
            }
            if (!reply.ok()) {
                state->error = state->error.isEmpty() ? ErrorOf(reply) : state->error;
            } else if (const auto entities = SpotifyProtocol::ParseMetadataReply(View(reply.body))) {
                for (const SpotifyProtocol::EntityData& entity : *entities) {
                    if (entity.status == 200 && !entity.data.empty()) {
                        state->found.insert(QString::fromStdString(entity.uri), entity.data);
                    }
                }
            } else {
                const QString unreadable = self->unreadable();
                state->error = state->error.isEmpty() ? unreadable : state->error;
            }
            if (--state->waiting == 0) {
                done(state->error.isEmpty() ? state->found : TEntities{}, state->error);
            }
        });
    }
}

void Library::tracks(const QStringList& uris, TDone<QList<Core::Track>> done) {
    QStringList unique = uris;
    unique.removeDuplicates();
    metadata(unique, ExtensionKind::TrackV4, [uris, done](const TEntities& entities, const QString& error) {
        if (!error.isEmpty()) {
            return done({}, error);
        }
        QList<Core::Track> result;
        for (const QString& uri : uris) {
            const auto found = entities.constFind(uri);
            if (found == entities.cend()) {
                continue;
            }
            if (const std::optional<SpotifyProtocol::TrackInfo> track = SpotifyProtocol::ParseTrack(*found)) {
                result << TrackOf(*track);
            }
        }
        done(result, {});
    });
}

void Library::collection(const QString& set, TDone<QStringList> done) {
    collectionPage(set, {}, {}, std::move(done));
}

void Library::collectionPage(const QString& set, const QString& token, QStringList uris, TDone<QStringList> done) {
    const QByteArray body = Bytes(SpotifyProtocol::CollectionPageRequest(
        session->username().toStdString(), set.toStdString(), token.toStdString(), kCollectionPage
    ));
    const WebRequest request{
        .method = "POST",
        .path = QStringLiteral("/collection/v2/paging"),
        .body = body,
        .contentType = SpotifyProtocol::kCollectionContentType,
        .accept = SpotifyProtocol::kCollectionContentType,
        .accountQuery = false,
    };
    QPointer<Library> self(this);
    web->spclient(request, [self, set, uris, done](const WebReply& reply) mutable {
        if (!self) {
            return;
        }
        if (!reply.ok()) {
            return done({}, ErrorOf(reply));
        }
        const std::optional<SpotifyProtocol::CollectionPage> page = SpotifyProtocol::ParseCollectionPage(View(reply.body
        ));
        if (!page) {
            return done({}, self->unreadable());
        }
        for (const SpotifyProtocol::CollectionItem& item : page->items) {
            uris << QString::fromStdString(item.uri);
        }
        if (page->nextPageToken.empty() || page->items.empty()) {
            return done(uris, {});
        }
        self->collectionPage(set, QString::fromStdString(page->nextPageToken), uris, done);
    });
}

void Library::likedTracks(TDone<QList<Core::Track>> done) {
    QPointer<Library> self(this);
    collection(QStringLiteral("collection"), [self, done](const QStringList& uris, const QString& error) {
        if (!self) {
            return;
        }
        if (!error.isEmpty()) {
            return done({}, error);
        }
        self->tracks(WithPrefix(uris, kTrackPrefix), done);
    });
}

void Library::listUris(const QString& path, int from, QList<Item> items, TDone<QList<Item>> done) {
    const QString pathAndPage = QStringLiteral("%1&from=%2&length=%3").arg(path).arg(from).arg(kListPage);
    QPointer<Library> self(this);
    web->spclient(WebRequest{.path = pathAndPage}, [self, path, from, items, done](const WebReply& reply) mutable {
        if (!self) {
            return;
        }
        if (!reply.ok()) {
            return done({}, ErrorOf(reply));
        }
        const std::optional<SpotifyProtocol::ListPage> page = SpotifyProtocol::ParseListPage(View(reply.body));
        if (!page) {
            return done({}, self->unreadable());
        }
        for (const SpotifyProtocol::ListItem& item : page->items) {
            items << Item{QString::fromStdString(item.uri), QString::fromStdString(item.name)};
        }
        if (from + kListPage >= page->length) {
            return done(items, {});
        }
        self->listUris(path, from + kListPage, items, done);
    });
}

void Library::playlists(TDone<QList<Item>> done) {
    const QString user = QString::fromLatin1(QUrl::toPercentEncoding(session->username()));
    const QString path = QStringLiteral("/playlist/v2/user/%1/rootlist?decorate=%2").arg(user, kRootlistDecoration);
    listUris(path, 0, {}, [done](const QList<Item>& items, const QString& error) {
        QList<Item> playlists;
        std::copy_if(items.cbegin(), items.cend(), std::back_inserter(playlists), [](const Item& item) {
            return item.uri.startsWith(QStringLiteral("spotify:playlist:"));
        });
        done(playlists, error);
    });
}

void Library::playlistTracks(const QString& uri, TDone<QList<Core::Track>> done) {
    const QString path =
        QStringLiteral("/playlist/v2/playlist/%1?decorate=revision,attributes,length,owner").arg(IdOfUri(uri));
    QPointer<Library> self(this);
    listUris(path, 0, {}, [self, done](const QList<Item>& items, const QString& error) {
        if (!self) {
            return;
        }
        if (!error.isEmpty()) {
            return done({}, error);
        }
        QStringList uris;
        for (const Item& item : items) {
            uris << item.uri;
        }
        self->tracks(WithPrefix(uris, kTrackPrefix), done);
    });
}

void Library::albumTracks(const QString& uri, TDone<QList<Core::Track>> done) {
    QPointer<Library> self(this);
    metadata({uri}, ExtensionKind::AlbumV4, [self, uri, done](const TEntities& entities, const QString& error) {
        if (!self) {
            return;
        }
        if (!error.isEmpty()) {
            return done({}, error);
        }
        std::optional<SpotifyProtocol::AlbumInfo> album;
        if (entities.contains(uri)) {
            album = SpotifyProtocol::ParseAlbum(entities.value(uri));
        }
        if (!album) {
            return done({}, {});
        }
        QStringList uris;
        for (const SpotifyProtocol::TGid& track : album->tracks) {
            uris << TrackUriOf(track);
        }
        self->tracks(uris, done);
    });
}

void Library::artistTopTracks(const QString& uri, TDone<QList<Core::Track>> done) {
    QPointer<Library> self(this);
    metadata({uri}, ExtensionKind::ArtistV4, [self, uri, done](const TEntities& entities, const QString& error) {
        if (!self) {
            return;
        }
        if (!error.isEmpty()) {
            return done({}, error);
        }
        std::optional<SpotifyProtocol::ArtistInfo> artist;
        if (entities.contains(uri)) {
            artist = SpotifyProtocol::ParseArtist(entities.value(uri));
        }
        if (!artist) {
            return done({}, {});
        }
        QStringList uris;
        for (const SpotifyProtocol::TGid& track :
             SpotifyProtocol::TopTracksIn(*artist, self->session->country().toStdString())) {
            if (uris.size() < kTopTracks) {
                uris << TrackUriOf(track);
            }
        }
        self->tracks(uris, done);
    });
}

void Library::names(const QStringList& uris, ExtensionKind kind, TDone<QList<Item>> done) {
    metadata(uris, kind, [uris, kind, done](const TEntities& entities, const QString& error) {
        if (!error.isEmpty()) {
            return done({}, error);
        }
        QList<Item> items;
        for (const QString& uri : uris) {
            const auto found = entities.constFind(uri);
            if (found == entities.cend()) {
                continue;
            }
            std::string name;
            if (kind == ExtensionKind::AlbumV4) {
                const std::optional<SpotifyProtocol::AlbumInfo> album = SpotifyProtocol::ParseAlbum(*found);
                name = album ? album->name : std::string();
            } else {
                const std::optional<SpotifyProtocol::ArtistInfo> artist = SpotifyProtocol::ParseArtist(*found);
                name = artist ? artist->name : std::string();
            }
            if (!name.empty()) {
                items << Item{uri, QString::fromStdString(name)};
            }
        }
        done(items, {});
    });
}

void Library::savedAlbums(TDone<QList<Item>> done) {
    QPointer<Library> self(this);
    collection(QStringLiteral("collection"), [self, done](const QStringList& uris, const QString& error) {
        if (!self) {
            return;
        }
        if (!error.isEmpty()) {
            return done({}, error);
        }
        self->names(WithPrefix(uris, kAlbumPrefix), ExtensionKind::AlbumV4, done);
    });
}

void Library::followedArtists(TDone<QList<Item>> done) {
    QPointer<Library> self(this);
    collection(QStringLiteral("artist"), [self, done](const QStringList& uris, const QString& error) {
        if (!self) {
            return;
        }
        if (!error.isEmpty()) {
            return done({}, error);
        }
        self->names(WithPrefix(uris, kArtistPrefix), ExtensionKind::ArtistV4, done);
    });
}

void Library::pathfinder(
    const QString& operation,
    const QStringList& hashes,
    const QJsonObject& variables,
    int attempt,
    TDone<QJsonObject> done
) {
    const QJsonObject persisted{
        {QStringLiteral("version"), 1},
        {QStringLiteral("sha256Hash"), hashes.value(attempt)},
    };
    const QJsonObject query{
        {QStringLiteral("variables"), variables},
        {QStringLiteral("operationName"), operation},
        {QStringLiteral("extensions"), QJsonObject{{QStringLiteral("persistedQuery"), persisted}}},
    };
    QPointer<Library> self(this);
    const QByteArray body = QJsonDocument(query).toJson(QJsonDocument::Compact);
    web->pathfinder(body, [self, operation, hashes, variables, attempt, done](const WebReply& reply) {
        if (!self) {
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply.body).object();
        if (!reply.network && StaleQuery(reply, root)) {
            if (attempt + 1 < hashes.size()) {
                return self->pathfinder(operation, hashes, variables, attempt + 1, done);
            }
            self->web->report(SpotifyClient::WebProblem::QueryOutdated);
        }
        if (!reply.ok()) {
            return done({}, ErrorOf(reply));
        }
        if (!root.value(QStringLiteral("data")).isObject()) {
            return done({}, Unreadable());
        }
        done(root.value(QStringLiteral("data")).toObject(), {});
    });
}

void Library::forYou(TDone<QList<Item>> done) {
    const QByteArray zone = QTimeZone::systemTimeZoneId();
    const QJsonObject variables{
        {QStringLiteral("homeEndUserIntegration"), QStringLiteral("INTEGRATION_DESKTOP")},
        {QStringLiteral("timeZone"), zone.isEmpty() ? QStringLiteral("UTC") : QString::fromLatin1(zone)},
        {QStringLiteral("sp_t"), QString()},
        {QStringLiteral("facet"), QString()},
        {QStringLiteral("sectionItemsLimit"), kForYouLimit},
        {QStringLiteral("includeEpisodeContentRatingsV2"), true},
    };
    pathfinder(
        QStringLiteral("home"),
        kHomeHashes,
        variables,
        0,
        [self = QPointer<Library>(this), done](const QJsonObject& data, const QString& error) {
            if (!self) {
                return;
            }
            if (!error.isEmpty()) {
                return done({}, error);
            }
            const std::optional<QList<Item>> playlists = MadeForYouOf(data);
            done(playlists.value_or(QList<Item>()), playlists ? QString() : self->unreadable());
        }
    );
}

void Library::search(const QString& text, TDone<Found> done) {
    const QJsonObject variables{
        {QStringLiteral("searchTerm"), text},
        {QStringLiteral("offset"), 0},
        {QStringLiteral("limit"), kSearchLimit},
        {QStringLiteral("numberOfTopResults"), 5},
        {QStringLiteral("includeAudiobooks"), false},
        {QStringLiteral("includeArtistHasConcertsField"), false},
        {QStringLiteral("includePreReleases"), false},
        {QStringLiteral("includeLocalConcertsField"), false},
        {QStringLiteral("includeAuthors"), false},
    };
    pathfinder(
        QStringLiteral("searchDesktop"),
        kSearchHashes,
        variables,
        0,
        [self = QPointer<Library>(this), done](const QJsonObject& data, const QString& error) {
            if (!self) {
                return;
            }
            if (!error.isEmpty()) {
                return done({}, error);
            }
            const std::optional<Found> found = FoundOf(data);
            done(found.value_or(Found{}), found ? QString() : self->unreadable());
        }
    );
}

void Library::context(const QByteArray& method, const QString& path, const QByteArray& body, TDone<ContextPage> done) {
    const WebRequest request{
        .method = method,
        .path = path,
        .body = body,
        .contentType = body.isEmpty() ? QByteArray() : kProtobuf,
        .accept = kJson,
        .accountQuery = false,
    };
    QPointer<Library> self(this);
    web->spclient(request, [self, done](const WebReply& reply) {
        if (!self) {
            return;
        }
        if (!reply.ok()) {
            return done({}, ErrorOf(reply));
        }
        const std::optional<ContextUris> context = ContextOf(reply.body);
        if (!context) {
            return done({}, self->unreadable());
        }
        const QString next = context->next;
        self->tracks(context->uris, [done, next](const QList<Core::Track>& tracks, const QString& error) {
            done(ContextPage{tracks, error.isEmpty() ? next : QString()}, error);
        });
    });
}

void Library::station(const QString& trackUri, TDone<ContextPage> done) {
    context(
        "GET", QStringLiteral("/context-resolve/v1/spotify:station:track:") + IdOfUri(trackUri), {}, std::move(done)
    );
}

void Library::autoplay(const QString& contextUri, const QStringList& recentTrackUris, TDone<ContextPage> done) {
    const QByteArray body =
        Bytes(SpotifyProtocol::AutoplayRequest(contextUri.toStdString(), StdStrings(recentTrackUris)));
    context("POST", QStringLiteral("/context-resolve/v1/autoplay"), body, std::move(done));
}

void Library::nextPage(const QString& pageUrl, TDone<ContextPage> done) {
    if (!pageUrl.startsWith(kPagePrefix)) {
        done({}, Unreadable());
        return;
    }
    context("GET", pageUrl.mid(kPagePrefix.size() - 1), {}, std::move(done));
}

}  // namespace SpotifySource
