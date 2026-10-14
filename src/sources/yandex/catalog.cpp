#include "sources/yandex/catalog.h"

#include "core/player.h"
#include "core/queue_source.h"
#include "sources/yandex/tracks.h"
#include "sources/yandex/vibe.h"
#include "yandex/api_client.h"
#include "yandex/library.h"

#include <QList>
#include <QPointer>

#include <algorithm>
#include <functional>
#include <memory>

namespace YandexSource {

using Core::Player;
using Core::SourceNoticeCode;
using Yandex::Track;

namespace {

bool HasAvailable(const QList<Track>& tracks) {
    return std::any_of(tracks.cbegin(), tracks.cend(), [](const Track& track) { return track.available; });
}

Core::SourceNotice NoticeOf(SourceNoticeCode code, const QString& subject = {}, const QString& detail = {}) {
    return Core::SourceNotice{.code = code, .subject = subject, .detail = detail};
}

}  // namespace

Catalog::Catalog(Player* player, Yandex::Library* library, QObject* parent)
    : QObject(parent)
    , corePlayer(player)
    , yandexLibrary(library) {
    connect(library, &Yandex::Library::likesChanged, this, &Catalog::likesChanged);
}

Catalog::TTracksDone Catalog::queueLoader(const QString& title, quint64 ticket) {
    QPointer<Catalog> self(this);
    return [self, title, ticket](const QList<Track>& tracks, const QString& error) {
        if (!self || !self->corePlayer->isLatestSourceRequest(ticket)) {
            return;
        }
        if (!error.isEmpty()) {
            return self->notify(NoticeOf(SourceNoticeCode::Failed, title, error));
        }
        if (!HasAvailable(tracks)) {  // SRC-07, SRC-08
            return self->notify(NoticeOf(SourceNoticeCode::Empty, title));
        }
        self->corePlayer->setSource(Core::QueueSource{.title = title, .head = TracksOf(tracks)}, true);
    };
}

void Catalog::notify(const Core::SourceNotice& notice) {
    Q_EMIT noticed(notice);
}

void Catalog::playLikes(bool autoplay) {
    const QString title = tr("Liked");
    notify(NoticeOf(SourceNoticeCode::Loading, title));
    QPointer<Catalog> self(this);
    const quint64 ticket = corePlayer->newSourceRequest();
    yandexLibrary->likedTracks([self, title, autoplay, ticket](const QList<Track>& tracks, const QString& error) {
        if (!self || !self->corePlayer->isLatestSourceRequest(ticket)) {
            return;
        }
        if (!error.isEmpty()) {
            return self->notify(NoticeOf(SourceNoticeCode::Failed, title, error));
        }
        if (!HasAvailable(tracks)) {  // SRC-07, SRC-08
            return self->notify(NoticeOf(SourceNoticeCode::Empty, title));
        }
        const QList<Core::Track> head = TracksOf(tracks);
        self->corePlayer->setSource(Core::QueueSource{.title = title, .head = head}, autoplay);
        const auto available = std::count_if(head.cbegin(), head.cend(), [](const Core::Track& track) {
            return track.available;
        });
        self->notify(
            Core::SourceNotice{.code = SourceNoticeCode::Loaded, .subject = title, .count = static_cast<int>(available)}
        );
    });
}

void Catalog::playPlaylist(const Yandex::PlaylistReference& playlist) {
    yandexLibrary->playlistTracks(playlist, queueLoader(playlist.title, corePlayer->newSourceRequest()));
}

void Catalog::playRecommendations(const Yandex::PlaylistReference& playlist) {
    yandexLibrary->playlistRecommendations(
        playlist, queueLoader(tr("%1: similar").arg(playlist.title), corePlayer->newSourceRequest())
    );
}

void Catalog::playArtist(const QString& artistId, const QString& name) {
    yandexLibrary->artistTopTracks(artistId, queueLoader(name, corePlayer->newSourceRequest()));
}

void Catalog::playAlbum(const QString& albumId, const QString& title) {
    yandexLibrary->albumTracks(albumId, queueLoader(title, corePlayer->newSourceRequest()));
}

void Catalog::playWave(const QStringList& seeds, const QString& title) {
    Yandex::Library* library = yandexLibrary;
    notify(NoticeOf(SourceNoticeCode::Loading, title));
    QPointer<Catalog> self(this);
    const quint64 ticket = corePlayer->newSourceRequest();
    library
        ->startWave(seeds, [self, library, title, ticket, seeds](const Yandex::WaveBatch& batch, const QString& error) {
            if (!self || !self->corePlayer->isLatestSourceRequest(ticket)) {
                return;
            }
            if (!error.isEmpty()) {
                return self->notify(NoticeOf(SourceNoticeCode::VibeFailed, title, error));
            }
            if (!HasAvailable(batch.tracks)) {  // WAVE-03
                return self->notify(NoticeOf(SourceNoticeCode::Empty, title));
            }
            self->waveSeeds = seeds;
            const auto vibe = std::make_shared<Vibe>(library, batch, seeds.value(0));
            self->corePlayer->setSource(
                Core::QueueSource{.title = title, .continuation = vibe, .given = TracksOf(batch.tracks)}, true
            );
        });
}

void Catalog::playMyWave() {
    playWave({kMyWaveSeed}, tr("My Vibe"));
}

void Catalog::search(const QString& text) {
    Yandex::Library* library = yandexLibrary;
    const QString title = tr("Search: %1").arg(text);
    notify(NoticeOf(SourceNoticeCode::Searching, text));
    QPointer<Catalog> self(this);
    const quint64 ticket = corePlayer->newSourceRequest();
    library->search(text, [self, library, title, ticket](const Yandex::SearchResult& result, const QString& error) {
        if (!self || !self->corePlayer->isLatestSourceRequest(ticket)) {
            return;
        }
        if (!error.isEmpty()) {
            return self->notify(NoticeOf(SourceNoticeCode::SearchFailed, title, error));
        }
        if (result.bestKind == Yandex::SearchResult::Kind::Artist && !result.bestId.isEmpty()) {
            return library->artistTopTracks(result.bestId, self->queueLoader(result.bestName, ticket));
        }
        if (result.bestKind == Yandex::SearchResult::Kind::Album && !result.bestId.isEmpty()) {
            return library->albumTracks(result.bestId, self->queueLoader(result.bestName, ticket));
        }
        if (!HasAvailable(result.tracks)) {  // SRC-12
            return self->notify(NoticeOf(SourceNoticeCode::NothingFound, title));
        }
        self->corePlayer->setSource(Core::QueueSource{.title = title, .head = TracksOf(result.tracks)}, true);
    });
}

void Catalog::searchTracks(
    const QString& text,
    std::function<void(const QList<Core::Track>&, const Yandex::RequestError&)> callback
) {
    yandexLibrary->searchTracks(text, [callback](const QList<Track>& tracks, const Yandex::RequestError& error) {
        callback(TracksOf(tracks), error);
    });
}

bool Catalog::owns(const Core::TrackKey& key) const {
    return key.source == kSourceName;
}

bool Catalog::isLiked(const Core::TrackKey& key) const {
    return owns(key) && yandexLibrary->isLiked(CatalogId(key));
}

bool Catalog::isCurrent(const Core::TrackKey& key) const {
    const Core::Track* current = corePlayer->currentTrack();
    return current && owns(key) && CatalogId(current->key) == CatalogId(key);
}

void Catalog::setLiked(const Core::TrackKey& key, bool liked) {
    QPointer<Catalog> self(this);
    yandexLibrary->setLiked(CatalogId(key), liked, [self, liked](bool, const QString& error) {
        if (!self) {
            return;
        }
        if (!error.isEmpty()) {
            return self->notify(NoticeOf(SourceNoticeCode::Failed, {}, error));
        }
        self->notify(NoticeOf(liked ? SourceNoticeCode::AddedToLiked : SourceNoticeCode::RemovedFromLiked));
    });
}

void Catalog::dislikeAndSkip(const Core::TrackKey& key) {
    QPointer<Catalog> self(this);
    yandexLibrary->dislike(CatalogId(key), [self](bool, const QString& error) {
        if (!self) {
            return;
        }
        self->notify(NoticeOf(error.isEmpty() ? SourceNoticeCode::Disliked : SourceNoticeCode::Failed, {}, error));
    });
    if (isCurrent(key)) {
        corePlayer->next();
    }
}

}  // namespace YandexSource
