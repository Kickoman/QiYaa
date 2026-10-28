#include "sources/spotify/catalog.h"

#include "core/player.h"
#include "core/queue_source.h"
#include "sources/spotify/autoplay.h"
#include "sources/spotify/tracks.h"

#include <QPointer>

#include <algorithm>

namespace SpotifySource {

using Core::SourceNoticeCode;

namespace {

QString FirstAvailableUri(const QList<Core::Track>& tracks) {
    const auto first = std::find_if(tracks.cbegin(), tracks.cend(), [](const Core::Track& track) {
        return track.available;
    });
    return first != tracks.cend() ? UriOf(first->key) : QString();
}

int AvailableCount(const QList<Core::Track>& tracks) {
    return static_cast<int>(std::count_if(tracks.cbegin(), tracks.cend(), [](const Core::Track& track) {
        return track.available;
    }));
}

Core::SourceNotice NoticeOf(SourceNoticeCode code, const QString& subject = {}, const QString& detail = {}) {
    return Core::SourceNotice{.code = code, .subject = subject, .detail = detail};
}

Core::SearchPick PickOf(Core::SearchPick::Kind kind, const Item& item) {
    return Core::SearchPick{.kind = kind, .id = item.uri, .name = item.name};
}

Core::SearchResults ResultsOf(const Found& found, const QList<Core::Track>& tracks) {
    Core::SearchResults results;
    for (const Core::Track& track : tracks) {
        if (found.kind == Found::Kind::Track && UriOf(track.key) == found.best.uri && !results.bestTrack) {
            results.bestTrack = track;
        } else {
            results.tracks << track;
        }
    }
    if (found.kind == Found::Kind::Artist) {
        results.bestArtist = PickOf(Core::SearchPick::Kind::Artist, found.best);
    }
    if (found.kind == Found::Kind::Album) {
        results.bestAlbum = PickOf(Core::SearchPick::Kind::Album, found.best);
    }
    for (const Item& artist : found.artists) {
        results.artists << PickOf(Core::SearchPick::Kind::Artist, artist);
    }
    for (const FoundAlbum& album : found.albums) {
        Core::SearchPick pick = PickOf(Core::SearchPick::Kind::Album, album.album);
        pick.artists = album.artists;
        pick.year = album.year;
        pick.single = album.single;
        results.albums << pick;
        if (results.bestAlbum && results.bestAlbum->id == pick.id) {
            results.bestAlbum = pick;
        }
    }
    return Core::Shown(results);
}

}  // namespace

Catalog::Catalog(Core::Player* player, Library* library, QObject* parent)
    : QObject(parent)
    , corePlayer(player)
    , spotifyLibrary(library) { }

void Catalog::notify(const Core::SourceNotice& notice) {
    Q_EMIT noticed(notice);
}

Catalog::TTracksDone
Catalog::queueLoader(const QString& title, const QString& contextUri, quint64 ticket, bool counted) {
    QPointer<Catalog> self(this);
    return [self, title, contextUri, ticket, counted](const QList<Core::Track>& tracks, const QString& error) {
        if (!self || !self->corePlayer->isLatestSourceRequest(ticket)) {
            return;
        }
        if (!error.isEmpty()) {
            return self->notify(NoticeOf(SourceNoticeCode::Failed, title, error));
        }
        const int available = AvailableCount(tracks);
        if (available == 0) {  // SRC-07, SRC-08
            return self->notify(NoticeOf(SourceNoticeCode::Empty, title));
        }
        const Core::QueueSource source{
            .title = title,
            .head = tracks,
            .historyWave = Autoplay::Of(self->spotifyLibrary, contextUri),
        };
        self->corePlayer->setSource(source, true);
        if (counted) {
            self->notify(Core::SourceNotice{.code = SourceNoticeCode::Loaded, .subject = title, .count = available});
        }
    };
}

void Catalog::playLikes() {
    const QString title = tr("Liked Songs");
    const QString context = QStringLiteral("spotify:user:%1:collection").arg(spotifyLibrary->username());
    notify(NoticeOf(SourceNoticeCode::Loading, title));
    spotifyLibrary->likedTracks(queueLoader(title, context, corePlayer->newSourceRequest(), true));
}

void Catalog::playPlaylist(const Item& playlist) {
    spotifyLibrary->playlistTracks(
        playlist.uri, queueLoader(playlist.name, playlist.uri, corePlayer->newSourceRequest(), false)
    );
}

void Catalog::playAlbum(const Item& album) {
    spotifyLibrary->albumTracks(album.uri, queueLoader(album.name, album.uri, corePlayer->newSourceRequest(), false));
}

void Catalog::playArtist(const Item& artist) {
    spotifyLibrary->artistTopTracks(
        artist.uri, queueLoader(artist.name, artist.uri, corePlayer->newSourceRequest(), false)
    );
}

void Catalog::playRadio(const Core::Track& track) {
    const QString title = tr("%1: radio").arg(track.title);
    const QString trackUri = UriOf(track.key);
    notify(NoticeOf(SourceNoticeCode::Loading, title));
    QPointer<Catalog> self(this);
    const quint64 ticket = corePlayer->newSourceRequest();
    spotifyLibrary->station(trackUri, [self, title, trackUri, ticket](const ContextPage& page, const QString& error) {
        if (!self || !self->corePlayer->isLatestSourceRequest(ticket)) {
            return;
        }
        if (!error.isEmpty()) {
            return self->notify(NoticeOf(SourceNoticeCode::VibeFailed, title, error));
        }
        if (AvailableCount(page.tracks) == 0) {
            return self->notify(NoticeOf(SourceNoticeCode::Empty, title));
        }
        const Core::QueueSource source{
            .title = title,
            .continuation = Autoplay::Station(self->spotifyLibrary, trackUri, page.next),
            .given = page.tracks,
        };
        self->corePlayer->setSource(source, true);
    });
}

bool Catalog::canSearch() const {
    return loggedIn;
}

void Catalog::find(const QString& text, TFound done) {
    QPointer<Catalog> self(this);
    spotifyLibrary->search(text, [self, done](const Found& found, const QString& error) {
        if (!self) {
            return;
        }
        if (!error.isEmpty()) {
            return done({}, error);
        }
        QStringList uris = found.trackUris;
        if (found.kind == Found::Kind::Track && !uris.contains(found.best.uri)) {
            uris.prepend(found.best.uri);
        }
        if (uris.isEmpty()) {
            return done(ResultsOf(found, {}), {});
        }
        const auto described = [self, found, done](const QList<Core::Track>& tracks, const QString& tracksError) {
            if (!self) {
                return;
            }
            if (!tracksError.isEmpty()) {
                return done({}, tracksError);
            }
            done(ResultsOf(found, tracks), {});
        };
        self->spotifyLibrary->tracks(uris, described);
    });
}

void Catalog::open(const Core::SearchPick& pick, TTracks done) {
    QPointer<Catalog> self(this);
    const auto opened = [self, done](const QList<Core::Track>& tracks, const QString& error) {
        if (self) {
            QList<Core::Track> available = tracks;
            available.removeIf([](const Core::Track& track) { return !track.available; });
            done(available, error);
        }
    };
    if (pick.kind == Core::SearchPick::Kind::Artist) {
        spotifyLibrary->artistTopTracks(pick.id, opened);
    } else {
        spotifyLibrary->albumTracks(pick.id, opened);
    }
}

void Catalog::playPick(const Core::SearchPick& pick) {
    const Item item{pick.id, pick.name};
    if (pick.kind == Core::SearchPick::Kind::Artist) {
        playArtist(item);
    } else {
        playAlbum(item);
    }
}

void Catalog::playTracks(const QString& title, const QList<Core::Track>& tracks, int start, const TFrom& from) {
    corePlayer->newSourceRequest();
    const int available = AvailableCount(tracks);
    if (available == 0) {
        return notify(NoticeOf(SourceNoticeCode::Empty, title));
    }
    const QString context = from ? from->id : FirstAvailableUri(tracks);
    const Core::QueueSource source{
        .title = title,
        .head = tracks,
        .historyWave = Autoplay::Of(spotifyLibrary, context),
        .start = start,
    };
    corePlayer->setSource(source, true);
    notify(Core::SourceNotice{.code = SourceNoticeCode::Loaded, .subject = title, .count = available});
}

void Catalog::add(const QString& title, const QList<Core::Track>& tracks, Core::AddPlacement placement) {
    if (AvailableCount(tracks) == 0) {
        return;
    }
    const Core::QueueSource source{
        .title = title,
        .head = tracks,
        .historyWave = Autoplay::Of(spotifyLibrary, FirstAvailableUri(tracks)),
    };
    corePlayer->add(source, placement);
    notify(Core::AddedNotice(title, tracks, placement));
}

void Catalog::addAlbum(const Core::SearchPick& album, Core::AddPlacement placement) {
    QPointer<Catalog> self(this);
    const auto loaded = [self, album, placement](const QList<Core::Track>& tracks, const QString& error) {
        if (!self) {
            return;
        }
        if (!error.isEmpty()) {
            return self->notify(NoticeOf(SourceNoticeCode::Failed, album.name, error));
        }
        if (AvailableCount(tracks) == 0) {
            return self->notify(NoticeOf(SourceNoticeCode::Empty, album.name));
        }
        self->add(album.name, tracks, placement);
    };
    spotifyLibrary->albumTracks(album.id, loaded);
}

bool Catalog::owns(const Core::TrackKey& key) const {
    return key.source == kSourceName;
}

}  // namespace SpotifySource
