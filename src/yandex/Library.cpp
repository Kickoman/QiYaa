#include "yandex/Library.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>

namespace qiyaa::yandex {

namespace {
constexpr int kTracksPerRequest = 250;
constexpr int kArtistTopLimit = 100;

QString str(const QJsonObject& o, const char* key) {
    return ApiClient::idString(o.value(QLatin1String(key)));
}
}  // namespace

Library::Library(ApiClient* api, QObject* parent) : QObject(parent), m_api(api) {}

QString Library::userPath(const QString& rest) const {
    return QStringLiteral("/users/%1/%2").arg(m_account.uid, rest);
}

void Library::connectAccount(Callback<Account> cb) {
    QPointer<Library> self(this);
    m_api->accountStatus([self, cb](const Account& acc, const QString& err) {
        if (!self) return;
        if (err.isEmpty()) {
            self->m_account = acc;
            Q_EMIT self->accountChanged();
        }
        cb(acc, err);
    });
}

void Library::logout() {
    m_account = {};
    m_likedIds.clear();
    m_api->setToken({});
    Q_EMIT accountChanged();
    Q_EMIT likesChanged();
}

QList<Track> Library::parseTrackArray(const QJsonArray& arr) {
    QList<Track> out;
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        // Some endpoints wrap tracks: {"track": {...}} or {"id":..., "track": {...}}.
        const QJsonValue inner = o.value(QStringLiteral("track"));
        out << ApiClient::parseTrack(inner.isObject() ? inner : v);
    }
    return out;
}

WaveBatch Library::parseWaveBatch(const QJsonValue& result) {
    const QJsonObject r = result.toObject();
    WaveBatch b;
    b.sessionId = r.value(QStringLiteral("radioSessionId")).toString();
    b.batchId = r.value(QStringLiteral("batchId")).toString();
    b.tracks = parseTrackArray(r.value(QStringLiteral("sequence")).toArray());
    return b;
}

void Library::tracksByIds(const QStringList& ids, Callback<QList<Track>> cb) {
    tracksChunk(ids, {}, std::move(cb));
}

void Library::tracksChunk(QStringList remaining, QList<Track> acc, Callback<QList<Track>> cb) {
    if (remaining.isEmpty()) return cb(acc, {});
    const QStringList chunk = remaining.mid(0, kTracksPerRequest);
    remaining = remaining.mid(chunk.size());
    QPointer<Library> self(this);
    m_api->tracks(chunk, [self, remaining, acc, cb](const QList<Track>& tracks, const QString& err) mutable {
        if (!self) return;
        if (!err.isEmpty()) return cb(acc, err);
        acc += tracks;
        self->tracksChunk(remaining, acc, cb);
    });
}

void Library::likedTracks(Callback<QList<Track>> cb) {
    QPointer<Library> self(this);
    m_api->getJson(userPath(QStringLiteral("likes/tracks")), {}, [self, cb](const QJsonValue& r, const QString& err) {
        if (!self) return;
        if (!err.isEmpty()) return cb({}, err);
        QStringList ids;
        self->m_likedIds.clear();
        const QJsonArray arr = r.toObject().value(QStringLiteral("library")).toObject().value(QStringLiteral("tracks")).toArray();
        for (const QJsonValue& v : arr) {
            const QString id = str(v.toObject(), "id");
            if (id.isEmpty()) continue;
            ids << id;
            self->m_likedIds.insert(id);
        }
        Q_EMIT self->likesChanged();
        self->tracksByIds(ids, cb);
    });
}

void Library::userPlaylists(Callback<QList<PlaylistRef>> cb) {
    m_api->getJson(userPath(QStringLiteral("playlists/list")), {}, [cb](const QJsonValue& r, const QString& err) {
        if (!err.isEmpty()) return cb({}, err);
        QList<PlaylistRef> out;
        for (const QJsonValue& v : r.toArray()) {
            const QJsonObject o = v.toObject();
            PlaylistRef p;
            p.ownerUid = str(o, "uid");
            if (p.ownerUid.isEmpty()) p.ownerUid = str(o.value(QStringLiteral("owner")).toObject(), "uid");
            p.kind = str(o, "kind");
            p.title = o.value(QStringLiteral("title")).toString();
            p.trackCount = o.value(QStringLiteral("trackCount")).toInt();
            if (!p.kind.isEmpty()) out << p;
        }
        cb(out, {});
    });
}

void Library::playlistTracks(const PlaylistRef& playlist, Callback<QList<Track>> cb) {
    QPointer<Library> self(this);
    const QString path = QStringLiteral("/users/%1/playlists/%2").arg(playlist.ownerUid, playlist.kind);
    m_api->getJson(path, {}, [self, cb](const QJsonValue& r, const QString& err) {
        if (!self) return;
        if (!err.isEmpty()) return cb({}, err);
        const QJsonArray items = r.toObject().value(QStringLiteral("tracks")).toArray();
        // Items usually embed full track objects; fall back to fetching by id.
        QList<Track> embedded = parseTrackArray(items);
        const bool complete = std::all_of(embedded.cbegin(), embedded.cend(), [](const Track& t) { return !t.title.isEmpty(); });
        if (complete) return cb(embedded, {});
        QStringList ids;
        for (const QJsonValue& v : items) ids << str(v.toObject(), "id");
        self->tracksByIds(ids, cb);
    });
}

void Library::likedArtists(Callback<QList<NamedRef>> cb) {
    m_api->getJson(userPath(QStringLiteral("likes/artists")), {}, [cb](const QJsonValue& r, const QString& err) {
        if (!err.isEmpty()) return cb({}, err);
        QList<NamedRef> out;
        for (const QJsonValue& v : r.toArray()) {
            QJsonObject o = v.toObject();
            if (o.value(QStringLiteral("artist")).isObject()) o = o.value(QStringLiteral("artist")).toObject();
            const NamedRef a{str(o, "id"), o.value(QStringLiteral("name")).toString()};
            if (!a.id.isEmpty()) out << a;
        }
        cb(out, {});
    });
}

void Library::artistTopTracks(const QString& artistId, Callback<QList<Track>> cb) {
    QPointer<Library> self(this);
    m_api->getJson(QStringLiteral("/artists/%1/track-ids-by-rating").arg(artistId), {},
                   [self, cb](const QJsonValue& r, const QString& err) {
                       if (!self) return;
                       if (!err.isEmpty()) return cb({}, err);
                       QStringList ids;
                       for (const QJsonValue& v : r.toObject().value(QStringLiteral("tracks")).toArray())
                           ids << ApiClient::idString(v);
                       self->tracksByIds(ids.mid(0, kArtistTopLimit), cb);
                   });
}

void Library::likedAlbums(Callback<QList<NamedRef>> cb) {
    QPointer<Library> self(this);
    m_api->getJson(userPath(QStringLiteral("likes/albums")), {}, [self, cb](const QJsonValue& r, const QString& err) {
        if (!self) return;
        if (!err.isEmpty()) return cb({}, err);
        QStringList ids;
        for (const QJsonValue& v : r.toArray()) {
            QJsonObject o = v.toObject();
            if (o.value(QStringLiteral("album")).isObject()) o = o.value(QStringLiteral("album")).toObject();
            const QString id = str(o, "id");
            if (!id.isEmpty()) ids << id;
        }
        if (ids.isEmpty()) return cb({}, {});
        self->m_api->postForm(QStringLiteral("/albums"), {{QStringLiteral("album-ids"), ids.join(u',')}},
                              [cb](const QJsonValue& r, const QString& err) {
                                  if (!err.isEmpty()) return cb({}, err);
                                  QList<NamedRef> out;
                                  for (const QJsonValue& v : r.toArray()) {
                                      const QJsonObject o = v.toObject();
                                      if (o.value(QStringLiteral("type")).toString() == QLatin1String("podcast")) continue;
                                      const QJsonArray artists = o.value(QStringLiteral("artists")).toArray();
                                      QString name = o.value(QStringLiteral("title")).toString();
                                      if (!artists.isEmpty())
                                          name = artists.first().toObject().value(QStringLiteral("name")).toString() +
                                                 QStringLiteral(" - ") + name;
                                      out << NamedRef{str(o, "id"), name};
                                  }
                                  cb(out, {});
                              });
    });
}

void Library::albumTracks(const QString& albumId, Callback<QList<Track>> cb) {
    m_api->getJson(QStringLiteral("/albums/%1/with-tracks").arg(albumId), {}, [cb, albumId](const QJsonValue& r, const QString& err) {
        if (!err.isEmpty()) return cb({}, err);
        QList<Track> out;
        for (const QJsonValue& vol : r.toObject().value(QStringLiteral("volumes")).toArray())
            for (Track t : parseTrackArray(vol.toArray())) {
                if (t.albumId.isEmpty()) t.albumId = albumId;
                out << t;
            }
        cb(out, {});
    });
}

void Library::stations(Callback<QList<Station>> cb) {
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("language"), QStringLiteral("ru"));
    m_api->getJson(QStringLiteral("/rotor/stations/list"), q, [cb](const QJsonValue& r, const QString& err) {
        if (!err.isEmpty()) return cb({}, err);
        QList<Station> out;
        for (const QJsonValue& v : r.toArray()) {
            const QJsonObject st = v.toObject().value(QStringLiteral("station")).toObject();
            const QJsonObject id = st.value(QStringLiteral("id")).toObject();
            Station s;
            s.type = id.value(QStringLiteral("type")).toString();
            s.id = s.type + u':' + id.value(QStringLiteral("tag")).toString();
            s.name = st.value(QStringLiteral("name")).toString();
            if (!s.type.isEmpty()) out << s;
        }
        cb(out, {});
    });
}

void Library::startWave(const QStringList& seeds, Callback<WaveBatch> cb) {
    QJsonObject body{{QStringLiteral("seeds"), QJsonArray::fromStringList(seeds)},
                     {QStringLiteral("includeTracksInResponse"), true},
                     {QStringLiteral("includeWaveModel"), true},
                     {QStringLiteral("interactive"), true}};
    m_api->postJson(QStringLiteral("/rotor/session/new"), body, [cb](const QJsonValue& r, const QString& err) {
        if (!err.isEmpty()) return cb({}, err);
        const WaveBatch b = parseWaveBatch(r);
        if (b.sessionId.isEmpty()) return cb({}, QStringLiteral("no radio session"));
        cb(b, {});
    });
}

void Library::moreWave(const QString& sessionId, const QStringList& queue, Callback<WaveBatch> cb) {
    QJsonObject body{{QStringLiteral("queue"), QJsonArray::fromStringList(queue)}};
    m_api->postJson(QStringLiteral("/rotor/session/%1/tracks").arg(sessionId), body,
                    [cb, sessionId](const QJsonValue& r, const QString& err) {
                        if (!err.isEmpty()) return cb({}, err);
                        WaveBatch b = parseWaveBatch(r);
                        if (b.sessionId.isEmpty()) b.sessionId = sessionId;
                        cb(b, {});
                    });
}

void Library::search(const QString& text, Callback<SearchResult> cb) {
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("text"), text);
    q.addQueryItem(QStringLiteral("type"), QStringLiteral("all"));
    q.addQueryItem(QStringLiteral("page"), QStringLiteral("0"));
    m_api->getJson(QStringLiteral("/search"), q, [cb](const QJsonValue& r, const QString& err) {
        if (!err.isEmpty()) return cb({}, err);
        const QJsonObject o = r.toObject();
        SearchResult res;
        const QJsonObject best = o.value(QStringLiteral("best")).toObject();
        res.bestType = best.value(QStringLiteral("type")).toString();
        const QJsonObject item = best.value(QStringLiteral("result")).toObject();
        res.bestId = str(item, "id");
        res.bestName = item.value(item.contains(QStringLiteral("name")) ? QStringLiteral("name") : QStringLiteral("title")).toString();
        res.tracks = parseTrackArray(o.value(QStringLiteral("tracks")).toObject().value(QStringLiteral("results")).toArray());
        cb(res, {});
    });
}

void Library::setLiked(const QString& trackId, bool liked, Callback<bool> cb) {
    QPointer<Library> self(this);
    const QString path = userPath(liked ? QStringLiteral("likes/tracks/add-multiple") : QStringLiteral("likes/tracks/remove"));
    m_api->postForm(path, {{QStringLiteral("track-ids"), trackId}}, [self, trackId, liked, cb](const QJsonValue&, const QString& err) {
        if (!self) return;
        if (err.isEmpty()) {
            if (liked) self->m_likedIds.insert(trackId);
            else self->m_likedIds.remove(trackId);
            Q_EMIT self->likesChanged();
        }
        cb(err.isEmpty(), err);
    });
}

void Library::dislike(const QString& trackId, Callback<bool> cb) {
    QPointer<Library> self(this);
    m_api->postForm(userPath(QStringLiteral("dislikes/tracks/add-multiple")), {{QStringLiteral("track-ids"), trackId}},
                    [self, trackId, cb](const QJsonValue&, const QString& err) {
                        if (!self) return;
                        if (err.isEmpty() && self->m_likedIds.remove(trackId)) Q_EMIT self->likesChanged();
                        cb(err.isEmpty(), err);
                    });
}

}  // namespace qiyaa::yandex
