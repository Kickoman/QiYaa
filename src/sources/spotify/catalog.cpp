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

int AvailableCount(const QList<Core::Track>& tracks) {
    return static_cast<int>(std::count_if(tracks.cbegin(), tracks.cend(), [](const Core::Track& track) {
        return track.available;
    }));
}

Core::SourceNotice NoticeOf(SourceNoticeCode code, const QString& subject = {}, const QString& detail = {}) {
    return Core::SourceNotice{.code = code, .subject = subject, .detail = detail};
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

void Catalog::search(const QString& text) {
    const QString title = tr("Search: %1").arg(text);
    notify(NoticeOf(SourceNoticeCode::Searching, text));
    QPointer<Catalog> self(this);
    const quint64 ticket = corePlayer->newSourceRequest();
    spotifyLibrary->search(text, [self, title, ticket](const Found& found, const QString& error) {
        if (!self || !self->corePlayer->isLatestSourceRequest(ticket)) {
            return;
        }
        if (!error.isEmpty()) {
            return self->notify(NoticeOf(SourceNoticeCode::SearchFailed, title, error));
        }
        Library* library = self->spotifyLibrary;
        switch (found.kind) {
            case Found::Kind::Artist:
                return library->artistTopTracks(
                    found.best.uri, self->queueLoader(found.best.name, found.best.uri, ticket, false)
                );
            case Found::Kind::Album:
                return library->albumTracks(
                    found.best.uri, self->queueLoader(found.best.name, found.best.uri, ticket, false)
                );
            case Found::Kind::Tracks: break;
        }
        if (found.trackUris.isEmpty()) {  // SRC-12
            return self->notify(NoticeOf(SourceNoticeCode::NothingFound, title));
        }
        const QString context = found.trackUris.first();
        library->tracks(
            found.trackUris,
            [self, title, context, ticket](const QList<Core::Track>& tracks, const QString& tracksError) {
                if (!self || !self->corePlayer->isLatestSourceRequest(ticket)) {
                    return;
                }
                if (tracksError.isEmpty() && AvailableCount(tracks) == 0) {
                    return self->notify(NoticeOf(SourceNoticeCode::NothingFound, title));
                }
                self->queueLoader(title, context, ticket, false)(tracks, tracksError);
            }
        );
    });
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

bool Catalog::owns(const Core::TrackKey& key) const {
    return key.source == kSourceName;
}

}  // namespace SpotifySource
