#include "yandex/library.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <utility>

namespace Yandex {

namespace {
constexpr int kTracksPerRequest = 250;
constexpr int kArtistTopLimit = 100;

QString StringField(const QJsonObject& object, const char* key) {
    return ApiClient::IdString(object.value(QLatin1String(key)));
}
SearchResult::Kind ParseSearchKind(const QString& type) {
    if (type.isEmpty()) {
        return SearchResult::Kind::None;
    }
    if (type == QLatin1String("artist")) {
        return SearchResult::Kind::Artist;
    }
    if (type == QLatin1String("album")) {
        return SearchResult::Kind::Album;
    }
    if (type == QLatin1String("track")) {
        return SearchResult::Kind::Track;
    }
    if (type == QLatin1String("playlist")) {
        return SearchResult::Kind::Playlist;
    }
    return SearchResult::Kind::Other;
}

}  // namespace

Library::Library(ApiClient* api, QObject* parent)
    : QObject(parent)
    , apiClient(api) { }

QString Library::userPath(const QString& rest) const {
    return QStringLiteral("/users/%1/%2").arg(accountData.uid, rest);
}

void Library::connectAccount(TCallback<Account> callback) {
    QPointer<Library> self(this);
    apiClient->accountStatus([self, callback](const Account& account, const QString& error) {
        if (!self) {
            return;
        }
        if (error.isEmpty()) {
            self->accountData = account;
            Q_EMIT self->accountChanged();
        }
        callback(account, error);
    });
}

void Library::logout() {
    accountData = {};
    likedIds.clear();
    apiClient->setToken({});
    Q_EMIT accountChanged();
    Q_EMIT likesChanged();
}

QList<Track> Library::ParseTrackArray(const QJsonArray& items) {
    QList<Track> out;
    for (const QJsonValue& value : items) {
        const QJsonObject object = value.toObject();
        const QJsonValue inner = object.value(QStringLiteral("track"));
        out << ApiClient::ParseTrack(inner.isObject() ? inner : value);
    }
    return out;
}

WaveBatch Library::ParseWaveBatch(const QJsonValue& result) {
    const QJsonObject object = result.toObject();
    WaveBatch batch;
    batch.sessionId = object.value(QStringLiteral("radioSessionId")).toString();
    batch.batchId = object.value(QStringLiteral("batchId")).toString();
    batch.tracks = ParseTrackArray(object.value(QStringLiteral("sequence")).toArray());
    return batch;
}

void Library::tracksByIds(const QStringList& ids, TCallback<QList<Track>> callback) {
    tracksChunk(ids, {}, std::move(callback));
}

void Library::tracksChunk(
    QStringList remaining,
    QList<Track> fetchedTracks,
    TCallback<QList<Track>> callback
) {
    if (remaining.isEmpty()) {
        return callback(fetchedTracks, {});
    }
    const QStringList chunk = remaining.mid(0, kTracksPerRequest);
    remaining = remaining.mid(chunk.size());
    QPointer<Library> self(this);
    apiClient->tracks(
        chunk,
        [self, remaining, fetchedTracks,
         callback](const QList<Track>& tracks, const QString& error) mutable {
            if (!self) {
                return;
            }
            if (!error.isEmpty()) {
                return callback(fetchedTracks, error);
            }
            fetchedTracks += tracks;
            self->tracksChunk(remaining, fetchedTracks, callback);
        }
    );
}

void Library::likedTracks(TCallback<QList<Track>> callback) {
    QPointer<Library> self(this);
    apiClient->getJson(
        userPath(QStringLiteral("likes/tracks")), {},
        [self, callback](const QJsonValue& result, const QString& error) {
            if (!self) {
                return;
            }
            if (!error.isEmpty()) {
                return callback({}, error);
            }
            QStringList ids;
            self->likedIds.clear();
            const QJsonArray libraryTracks = result.toObject()
                                                 .value(QStringLiteral("library"))
                                                 .toObject()
                                                 .value(QStringLiteral("tracks"))
                                                 .toArray();
            for (const QJsonValue& value : libraryTracks) {
                const QString id = StringField(value.toObject(), "id");
                if (id.isEmpty()) {
                    continue;
                }
                ids << id;
                self->likedIds.insert(id);
            }
            Q_EMIT self->likesChanged();
            self->tracksByIds(ids, callback);
        }
    );
}

void Library::userPlaylists(TCallback<QList<PlaylistReference>> callback) {
    apiClient->getJson(
        userPath(QStringLiteral("playlists/list")), {},
        [callback](const QJsonValue& result, const QString& error) {
            if (!error.isEmpty()) {
                return callback({}, error);
            }
            QList<PlaylistReference> out;
            for (const QJsonValue& value : result.toArray()) {
                const QJsonObject object = value.toObject();
                PlaylistReference playlist;
                playlist.ownerUid = StringField(object, "uid");
                if (playlist.ownerUid.isEmpty()) {
                    playlist.ownerUid =
                        StringField(object.value(QStringLiteral("owner")).toObject(), "uid");
                }
                playlist.kind = StringField(object, "kind");
                playlist.title = object.value(QStringLiteral("title")).toString();
                playlist.trackCount = object.value(QStringLiteral("trackCount")).toInt();
                if (!playlist.kind.isEmpty()) {
                    out << playlist;
                }
            }
            callback(out, {});
        }
    );
}

void Library::tracksFromItems(const QJsonArray& items, TCallback<QList<Track>> callback) {
    const QList<Track> embedded = ParseTrackArray(items);
    const bool complete = std::all_of(embedded.cbegin(), embedded.cend(), [](const Track& track) {
        return !track.title.isEmpty();
    });
    if (complete) {
        return callback(embedded, {});
    }
    QStringList ids;
    for (const QJsonValue& value : items) {
        ids
            << (value.isObject() ? StringField(value.toObject(), "id") : ApiClient::IdString(value)
               );
    }
    tracksByIds(ids, callback);
}

void Library::playlistTracks(const PlaylistReference& playlist, TCallback<QList<Track>> callback) {
    QPointer<Library> self(this);
    const QString path =
        QStringLiteral("/users/%1/playlists/%2").arg(playlist.ownerUid, playlist.kind);
    apiClient->getJson(path, {}, [self, callback](const QJsonValue& result, const QString& error) {
        if (!self) {
            return;
        }
        if (!error.isEmpty()) {
            return callback({}, error);
        }
        self->tracksFromItems(
            result.toObject().value(QStringLiteral("tracks")).toArray(), callback
        );
    });
}

void Library::playlistRecommendations(
    const PlaylistReference& playlist,
    TCallback<QList<Track>> callback
) {
    QPointer<Library> self(this);
    const QString path = QStringLiteral("/users/%1/playlists/%2/recommendations")
                             .arg(playlist.ownerUid, playlist.kind);
    apiClient->getJson(path, {}, [self, callback](const QJsonValue& result, const QString& error) {
        if (!self) {
            return;
        }
        if (!error.isEmpty()) {
            return callback({}, error);
        }
        self->tracksFromItems(
            result.toObject().value(QStringLiteral("tracks")).toArray(), callback
        );
    });
}

void Library::personalPlaylists(TCallback<QList<PlaylistReference>> callback) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("blocks"), QStringLiteral("personalplaylists"));
    apiClient->getJson(
        QStringLiteral("/landing3"), query,
        [callback](const QJsonValue& result, const QString& error) {
            if (!error.isEmpty()) {
                return callback({}, error);
            }
            QList<PlaylistReference> out;
            for (const QJsonValue& block :
                 result.toObject().value(QStringLiteral("blocks")).toArray()) {
                for (const QJsonValue& entity :
                     block.toObject().value(QStringLiteral("entities")).toArray()) {
                    QJsonObject playlistObject =
                        entity.toObject().value(QStringLiteral("data")).toObject();
                    if (playlistObject.value(QStringLiteral("data")).isObject()) {
                        playlistObject = playlistObject.value(QStringLiteral("data")).toObject();
                    }
                    PlaylistReference playlist;
                    playlist.ownerUid = StringField(playlistObject, "uid");
                    if (playlist.ownerUid.isEmpty()) {
                        playlist.ownerUid = StringField(
                            playlistObject.value(QStringLiteral("owner")).toObject(), "uid"
                        );
                    }
                    playlist.kind = StringField(playlistObject, "kind");
                    playlist.title = playlistObject.value(QStringLiteral("title")).toString();
                    playlist.trackCount =
                        playlistObject.value(QStringLiteral("trackCount")).toInt();
                    if (!playlist.ownerUid.isEmpty() && !playlist.kind.isEmpty()) {
                        out << playlist;
                    }
                }
            }
            callback(out, {});
        }
    );
}

void Library::wheelWaves(const QStringList& seeds, TCallback<QList<Wave>> callback) {
    const QJsonObject body{
        {QStringLiteral("context"),
         QJsonObject{
             {QStringLiteral("type"), QStringLiteral("WAVE")},
             {QStringLiteral("data"),
              QJsonObject{{QStringLiteral("seeds"), QJsonArray::fromStringList(seeds)}}}
         }},
        {QStringLiteral("feedbacks"), QJsonArray{}}
    };
    apiClient->postJson(
        QStringLiteral("/wheel/new"), body,
        [callback](const QJsonValue& result, const QString& error) {
            if (!error.isEmpty()) {
                return callback({}, error);
            }
            QList<Wave> out;
            for (const QJsonValue& value :
                 result.toObject().value(QStringLiteral("items")).toArray()) {
                const QJsonObject item = value.toObject();
                const QJsonObject waveObject = item.value(QStringLiteral("data"))
                                                   .toObject()
                                                   .value(QStringLiteral("wave"))
                                                   .toObject();
                if (item.value(QStringLiteral("type")).toString() != QLatin1String("WAVE")
                    || waveObject.isEmpty()) {
                    continue;
                }
                Wave wave;
                wave.name = waveObject.value(QStringLiteral("name")).toString();
                wave.description = waveObject.value(QStringLiteral("description")).toString();
                for (const QJsonValue& seed : waveObject.value(QStringLiteral("seeds")).toArray()) {
                    wave.seeds << seed.toString();
                }
                if (!wave.name.isEmpty() && !wave.seeds.isEmpty()) {
                    out << wave;
                }
            }
            callback(out, {});
        }
    );
}

QString Library::WaveEventName(WaveEvent event) {
    switch (event) {
        case WaveEvent::RadioStarted: return QStringLiteral("radioStarted");
        case WaveEvent::TrackStarted: return QStringLiteral("trackStarted");
        case WaveEvent::TrackFinished: return QStringLiteral("trackFinished");
        case WaveEvent::Skip: return QStringLiteral("skip");
    }
    return {};
}

void Library::waveFeedback(
    const QString& sessionId,
    const QString& stationId,
    const QString& batchId,
    WaveEvent event,
    const Track* track,
    double playedSeconds
) {
    QJsonObject eventObject{
        {QStringLiteral("type"), WaveEventName(event)},
        {QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}
    };
    if (event == WaveEvent::RadioStarted) {
        eventObject.insert(QStringLiteral("from"), QStringLiteral("web-main-rup-radio-main"));
    }
    if (track) {
        eventObject.insert(
            QStringLiteral("trackId"),
            track->albumId.isEmpty() ? track->id : track->id + u':' + track->albumId
        );
    }
    if (event == WaveEvent::TrackFinished || event == WaveEvent::Skip) {
        eventObject.insert(
            QStringLiteral("totalPlayedSeconds"), std::round(playedSeconds * 10) / 10
        );
    }

    QPointer<Library> self(this);
    auto viaStation = [self, stationId, batchId, eventObject] {
        if (!self || stationId.isEmpty()) {
            return;
        }
        QString path = QStringLiteral("/rotor/station/%1/feedback").arg(stationId);
        if (!batchId.isEmpty()) {
            path += QStringLiteral("?batch-id=")
                + QString::fromLatin1(QUrl::toPercentEncoding(batchId));
        }
        self->apiClient->postJson(
            path, eventObject,
            [type = eventObject.value(QStringLiteral("type")).toString()](
                const QJsonValue&, const QString& error
            ) {
                if (!error.isEmpty()) {
                    qWarning(
                        "wave feedback (station) %s failed: %s", qPrintable(type), qPrintable(error)
                    );
                }
            }
        );
    };
    if (sessionId.isEmpty() || stationFeedbackSessions.contains(sessionId)) {
        return viaStation();
    }

    QJsonObject body{{QStringLiteral("event"), eventObject}};
    if (!batchId.isEmpty()) {
        body.insert(QStringLiteral("batchId"), batchId);
    }
    apiClient->postJson(
        QStringLiteral("/rotor/session/%1/feedback").arg(sessionId), body,
        [self, sessionId, viaStation](const QJsonValue&, const QString& error) {
            if (!self || error.isEmpty()) {
                return;
            }
            if (!error.startsWith(QLatin1String("HTTP 4"))) {
                qWarning("wave feedback failed: %s", qPrintable(error));
                return;
            }
            qInfo(
                "wave feedback: session endpoint failed (%s), using the station endpoint",
                qPrintable(error)
            );
            self->stationFeedbackSessions.insert(sessionId);
            viaStation();
        }
    );
}

void Library::likedArtists(TCallback<QList<NamedReference>> callback) {
    apiClient->getJson(
        userPath(QStringLiteral("likes/artists")), {},
        [callback](const QJsonValue& result, const QString& error) {
            if (!error.isEmpty()) {
                return callback({}, error);
            }
            QList<NamedReference> out;
            for (const QJsonValue& value : result.toArray()) {
                QJsonObject object = value.toObject();
                if (object.value(QStringLiteral("artist")).isObject()) {
                    object = object.value(QStringLiteral("artist")).toObject();
                }
                const NamedReference artist{
                    StringField(object, "id"), object.value(QStringLiteral("name")).toString()
                };
                if (!artist.id.isEmpty()) {
                    out << artist;
                }
            }
            callback(out, {});
        }
    );
}

void Library::artistTopTracks(const QString& artistId, TCallback<QList<Track>> callback) {
    QPointer<Library> self(this);
    apiClient->getJson(
        QStringLiteral("/artists/%1/track-ids-by-rating").arg(artistId), {},
        [self, callback](const QJsonValue& result, const QString& error) {
            if (!self) {
                return;
            }
            if (!error.isEmpty()) {
                return callback({}, error);
            }
            QStringList ids;
            for (const QJsonValue& value :
                 result.toObject().value(QStringLiteral("tracks")).toArray()) {
                ids << ApiClient::IdString(value);
            }
            self->tracksByIds(ids.mid(0, kArtistTopLimit), callback);
        }
    );
}

void Library::likedAlbums(TCallback<QList<NamedReference>> callback) {
    QPointer<Library> self(this);
    apiClient->getJson(
        userPath(QStringLiteral("likes/albums")), {},
        [self, callback](const QJsonValue& result, const QString& error) {
            if (!self) {
                return;
            }
            if (!error.isEmpty()) {
                return callback({}, error);
            }
            QStringList ids;
            for (const QJsonValue& value : result.toArray()) {
                QJsonObject object = value.toObject();
                if (object.value(QStringLiteral("album")).isObject()) {
                    object = object.value(QStringLiteral("album")).toObject();
                }
                const QString id = StringField(object, "id");
                if (!id.isEmpty()) {
                    ids << id;
                }
            }
            if (ids.isEmpty()) {
                return callback({}, {});
            }
            self->apiClient->postForm(
                QStringLiteral("/albums"), {{QStringLiteral("album-ids"), ids.join(u',')}},
                [callback](const QJsonValue& albums, const QString& albumsError) {
                    if (!albumsError.isEmpty()) {
                        return callback({}, albumsError);
                    }
                    QList<NamedReference> out;
                    for (const QJsonValue& value : albums.toArray()) {
                        const QJsonObject object = value.toObject();
                        if (object.value(QStringLiteral("type")).toString()
                            == QLatin1String("podcast")) {
                            continue;
                        }
                        const QJsonArray artists =
                            object.value(QStringLiteral("artists")).toArray();
                        QString name = object.value(QStringLiteral("title")).toString();
                        if (!artists.isEmpty()) {
                            name =
                                artists.first().toObject().value(QStringLiteral("name")).toString()
                                + QStringLiteral(" - ") + name;
                        }
                        out << NamedReference{StringField(object, "id"), name};
                    }
                    callback(out, {});
                }
            );
        }
    );
}

void Library::albumTracks(const QString& albumId, TCallback<QList<Track>> callback) {
    apiClient->getJson(
        QStringLiteral("/albums/%1/with-tracks").arg(albumId), {},
        [callback, albumId](const QJsonValue& result, const QString& error) {
            if (!error.isEmpty()) {
                return callback({}, error);
            }
            QList<Track> out;
            for (const QJsonValue& volume :
                 result.toObject().value(QStringLiteral("volumes")).toArray()) {
                for (Track track : ParseTrackArray(volume.toArray())) {
                    if (track.albumId.isEmpty()) {
                        track.albumId = albumId;
                    }
                    out << track;
                }
            }
            callback(out, {});
        }
    );
}

void Library::stations(TCallback<QList<Station>> callback) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("language"), QStringLiteral("ru"));
    apiClient->getJson(
        QStringLiteral("/rotor/stations/list"), query,
        [callback](const QJsonValue& result, const QString& error) {
            if (!error.isEmpty()) {
                return callback({}, error);
            }
            QList<Station> out;
            for (const QJsonValue& value : result.toArray()) {
                const QJsonObject stationObject =
                    value.toObject().value(QStringLiteral("station")).toObject();
                const QJsonObject stationId = stationObject.value(QStringLiteral("id")).toObject();
                Station station;
                station.type = stationId.value(QStringLiteral("type")).toString();
                station.id =
                    station.type + u':' + stationId.value(QStringLiteral("tag")).toString();
                station.name = stationObject.value(QStringLiteral("name")).toString();
                if (!station.type.isEmpty()) {
                    out << station;
                }
            }
            callback(out, {});
        }
    );
}

void Library::startWave(const QStringList& seeds, TCallback<WaveBatch> callback) {
    QJsonObject body{
        {QStringLiteral("seeds"), QJsonArray::fromStringList(seeds)},
        {QStringLiteral("includeTracksInResponse"), true},
        {QStringLiteral("includeWaveModel"), true},
        {QStringLiteral("interactive"), true}
    };
    apiClient->postJson(
        QStringLiteral("/rotor/session/new"), body,
        [callback](const QJsonValue& result, const QString& error) {
            if (!error.isEmpty()) {
                return callback({}, error);
            }
            const WaveBatch batch = ParseWaveBatch(result);
            if (batch.sessionId.isEmpty()) {
                return callback(
                    {}, QStringLiteral("/rotor/session/new: the reply has no radioSessionId")
                );
            }
            callback(batch, {});
        }
    );
}

void Library::moreWave(
    const QString& sessionId,
    const QStringList& queue,
    TCallback<WaveBatch> callback
) {
    QJsonObject body{{QStringLiteral("queue"), QJsonArray::fromStringList(queue)}};
    apiClient->postJson(
        QStringLiteral("/rotor/session/%1/tracks").arg(sessionId), body,
        [callback, sessionId](const QJsonValue& result, const QString& error) {
            if (!error.isEmpty()) {
                return callback({}, error);
            }
            WaveBatch batch = ParseWaveBatch(result);
            if (batch.sessionId.isEmpty()) {
                batch.sessionId = sessionId;
            }
            callback(batch, {});
        }
    );
}

void Library::searchTracks(
    const QString& text,
    std::function<void(const QList<Track>& tracks, const RequestError& error)> callback
) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("text"), text);
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("track"));
    query.addQueryItem(QStringLiteral("page"), QStringLiteral("0"));
    apiClient->getClassifiedJson(
        QStringLiteral("/search"), query,
        [callback](const QJsonValue& result, const RequestError& error) {
            if (error.isError()) {
                return callback({}, error);
            }
            callback(
                ParseTrackArray(result.toObject()
                                    .value(QStringLiteral("tracks"))
                                    .toObject()
                                    .value(QStringLiteral("results"))
                                    .toArray()),
                error
            );
        }
    );
}

void Library::search(const QString& text, TCallback<SearchResult> callback) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("text"), text);
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("all"));
    query.addQueryItem(QStringLiteral("page"), QStringLiteral("0"));
    apiClient->getJson(
        QStringLiteral("/search"), query,
        [callback](const QJsonValue& result, const QString& error) {
            if (!error.isEmpty()) {
                return callback({}, error);
            }
            const QJsonObject object = result.toObject();
            SearchResult searchResult;
            const QJsonObject best = object.value(QStringLiteral("best")).toObject();
            searchResult.bestKind = ParseSearchKind(best.value(QStringLiteral("type")).toString());
            const QJsonObject item = best.value(QStringLiteral("result")).toObject();
            searchResult.bestId = StringField(item, "id");
            searchResult.bestName =
                item.value(
                        item.contains(QStringLiteral("name")) ? QStringLiteral("name")
                                                              : QStringLiteral("title")
                )
                    .toString();
            searchResult.tracks = ParseTrackArray(object.value(QStringLiteral("tracks"))
                                                      .toObject()
                                                      .value(QStringLiteral("results"))
                                                      .toArray());
            callback(searchResult, {});
        }
    );
}

void Library::setLiked(const QString& trackId, bool liked, TCallback<bool> callback) {
    QPointer<Library> self(this);
    const QString path = userPath(
        liked ? QStringLiteral("likes/tracks/add-multiple") : QStringLiteral("likes/tracks/remove")
    );
    apiClient->postForm(
        path, {{QStringLiteral("track-ids"), trackId}},
        [self, trackId, liked, callback](const QJsonValue&, const QString& error) {
            if (!self) {
                return;
            }
            if (error.isEmpty()) {
                if (liked) {
                    self->likedIds.insert(trackId);
                } else {
                    self->likedIds.remove(trackId);
                }
                Q_EMIT self->likesChanged();
            }
            callback(error.isEmpty(), error);
        }
    );
}

void Library::dislike(const QString& trackId, TCallback<bool> callback) {
    QPointer<Library> self(this);
    apiClient->postForm(
        userPath(QStringLiteral("dislikes/tracks/add-multiple")),
        {{QStringLiteral("track-ids"), trackId}},
        [self, trackId, callback](const QJsonValue&, const QString& error) {
            if (!self) {
                return;
            }
            if (error.isEmpty() && self->likedIds.remove(trackId)) {
                Q_EMIT self->likesChanged();
            }
            callback(error.isEmpty(), error);
        }
    );
}

}  // namespace Yandex
