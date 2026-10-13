#include "sources/yandex/catalog.h"

#include "core/jam_mode.h"
#include "core/player.h"
#include "sources/yandex/tracks.h"
#include "yandex/api_client.h"
#include "yandex/library.h"

#include <QCoreApplication>
#include <QHash>
#include <QList>
#include <QPointer>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>

namespace YandexSource {

using Core::Player;
using Yandex::Track;

namespace {

constexpr qsizetype kWaveHistory = 5;

bool HasAvailable(const QList<Track>& tracks) {
    return std::any_of(tracks.cbegin(), tracks.cend(), [](const Track& track) { return track.available; });
}

void ShowStatus(Player* player, const QString& text) {
    Q_EMIT player->statusMessage(text);
}

auto QueueLoader(Player* player, const QString& title, quint64 ticket) {
    QPointer<Player> guardedPlayer(player);
    return [guardedPlayer, title, ticket](const QList<Track>& tracks, const QString& error) {
        if (!guardedPlayer || !guardedPlayer->isLatestSourceRequest(ticket)) {
            return;
        }
        if (!error.isEmpty()) {
            return ShowStatus(
                guardedPlayer, QCoreApplication::translate("YandexSource::Catalog", "Error: %1").arg(error)
            );
        }
        if (!HasAvailable(tracks)) {  // SRC-07, SRC-08
            return ShowStatus(
                guardedPlayer, QCoreApplication::translate("YandexSource::Catalog", "%1: empty").arg(title)
            );
        }
        guardedPlayer->setQueue(TracksOf(tracks), title, true);
    };
}

struct WaveState {
    QString session;
    QString station;
    QHash<QString, QString> batchOfTrack;
};

Player::TLoadMoreCallback
WaveMore(QPointer<Player> player, Yandex::Library* library, std::shared_ptr<WaveState> state) {
    return [player, library, state](std::function<void(const QList<Core::Track>&)> done) {
        if (!player) {
            return;
        }
        QStringList queue;
        const QList<Core::Track>& playlist = player->playlist();
        for (qsizetype i = std::max<qsizetype>(0, playlist.size() - kWaveHistory); i < playlist.size(); ++i) {
            queue << CatalogId(playlist[i].key);
        }
        library->moreWave(
            state->session,
            queue,
            [done, state](const Yandex::WaveBatch& nextBatch, const QString& waveError) {
                if (!waveError.isEmpty()) {
                    qWarning("wave: %s", qPrintable(waveError));
                }
                for (const Track& track : nextBatch.tracks) {
                    state->batchOfTrack.insert(track.id, nextBatch.batchId);
                }
                done(TracksOf(nextBatch.tracks));
            }
        );
    };
}

Player::TEventCallback WaveEvents(Yandex::Library* library, std::shared_ptr<WaveState> state) {
    return [library, state](Player::TrackEvent event, const Core::Track& track, double played) {
        const std::optional<Track> yandexTrack = YandexTrackOf(track);
        library->waveFeedback(
            state->session,
            state->station,
            state->batchOfTrack.value(CatalogId(track.key)),
            WaveEventOf(event),
            yandexTrack ? &*yandexTrack : nullptr,
            played
        );
    };
}

}  // namespace

Catalog::Catalog(Player* player, Yandex::Library* library, QObject* parent)
    : QObject(parent)
    , corePlayer(player)
    , yandexLibrary(library) {
    connect(library, &Yandex::Library::likesChanged, this, &Catalog::likesChanged);
}

void Catalog::showStatus(const QString& text) {
    ShowStatus(corePlayer, text);
}

bool Catalog::refusedForJam() {
    if (!jamMode || !jamMode->isActive()) {
        return false;
    }
    showStatus(tr("A jam is on: add tracks to the jam"));
    return true;
}

void Catalog::playLikes(bool autoplay) {
    if (refusedForJam()) {
        return;
    }
    showStatus(tr("Liked: loading…"));
    QPointer<Player> guardedPlayer(corePlayer);
    const quint64 ticket = corePlayer->newSourceRequest();
    yandexLibrary->likedTracks([guardedPlayer, autoplay, ticket](const QList<Track>& tracks, const QString& error) {
        if (!guardedPlayer || !guardedPlayer->isLatestSourceRequest(ticket)) {
            return;
        }
        if (!error.isEmpty()) {
            return ShowStatus(guardedPlayer, tr("Error: %1").arg(error));
        }
        if (!HasAvailable(tracks)) {  // SRC-07, SRC-08
            return ShowStatus(guardedPlayer, tr("Liked: empty"));
        }
        guardedPlayer->setQueue(TracksOf(tracks), tr("Liked"), autoplay);
        ShowStatus(
            guardedPlayer, tr("Liked: %n track(s)", nullptr, static_cast<int>(guardedPlayer->playlist().size()))
        );
    });
}

void Catalog::playPlaylist(const Yandex::PlaylistReference& playlist) {
    if (refusedForJam()) {
        return;
    }
    yandexLibrary->playlistTracks(playlist, QueueLoader(corePlayer, playlist.title, corePlayer->newSourceRequest()));
}

void Catalog::playRecommendations(const Yandex::PlaylistReference& playlist) {
    if (refusedForJam()) {
        return;
    }
    yandexLibrary->playlistRecommendations(
        playlist, QueueLoader(corePlayer, tr("%1: similar").arg(playlist.title), corePlayer->newSourceRequest())
    );
}

void Catalog::playArtist(const QString& artistId, const QString& name) {
    if (refusedForJam()) {
        return;
    }
    yandexLibrary->artistTopTracks(artistId, QueueLoader(corePlayer, name, corePlayer->newSourceRequest()));
}

void Catalog::playAlbum(const QString& albumId, const QString& title) {
    if (refusedForJam()) {
        return;
    }
    yandexLibrary->albumTracks(albumId, QueueLoader(corePlayer, title, corePlayer->newSourceRequest()));
}

void Catalog::playWave(const QStringList& seeds, const QString& title) {
    if (refusedForJam()) {
        return;
    }
    Yandex::Library* library = yandexLibrary;
    showStatus(tr("%1: loading…").arg(title));
    QPointer<Player> guardedPlayer(corePlayer);
    QPointer<Catalog> self(this);
    const quint64 ticket = corePlayer->newSourceRequest();
    library->startWave(
        seeds,
        [self, guardedPlayer, library, title, ticket, seeds](const Yandex::WaveBatch& batch, const QString& error) {
            if (!guardedPlayer || !guardedPlayer->isLatestSourceRequest(ticket)) {
                return;
            }
            if (!error.isEmpty()) {
                return ShowStatus(guardedPlayer, tr("Vibe error: %1").arg(error));
            }
            if (!HasAvailable(batch.tracks)) {  // WAVE-03
                return ShowStatus(guardedPlayer, tr("%1: empty").arg(title));
            }
            auto state = std::make_shared<WaveState>();
            state->session = batch.sessionId;
            state->station = seeds.value(0);
            for (const Track& track : batch.tracks) {
                state->batchOfTrack.insert(track.id, batch.batchId);
            }
            if (self) {
                self->waveSeeds = seeds;
            }
            library->waveFeedback(state->session, state->station, batch.batchId, Yandex::WaveEvent::RadioStarted);
            guardedPlayer->setQueue(
                TracksOf(batch.tracks), title, true, WaveMore(guardedPlayer, library, state), WaveEvents(library, state)
            );
        }
    );
}

void Catalog::playMyWave() {
    playWave({kMyWaveSeed}, tr("My Vibe"));
}

void Catalog::search(const QString& text) {
    if (refusedForJam()) {
        return;
    }
    Yandex::Library* library = yandexLibrary;
    const QString title = tr("Search: %1").arg(text);
    showStatus(title + QStringLiteral("…"));
    QPointer<Player> guardedPlayer(corePlayer);
    const quint64 ticket = corePlayer->newSourceRequest();
    library->search(
        text,
        [guardedPlayer, library, title, ticket](const Yandex::SearchResult& result, const QString& error) {
            if (!guardedPlayer || !guardedPlayer->isLatestSourceRequest(ticket)) {
                return;
            }
            if (!error.isEmpty()) {
                return ShowStatus(guardedPlayer, tr("Search error: %1").arg(error));
            }
            if (result.bestKind == Yandex::SearchResult::Kind::Artist && !result.bestId.isEmpty()) {
                return library->artistTopTracks(result.bestId, QueueLoader(guardedPlayer, result.bestName, ticket));
            }
            if (result.bestKind == Yandex::SearchResult::Kind::Album && !result.bestId.isEmpty()) {
                return library->albumTracks(result.bestId, QueueLoader(guardedPlayer, result.bestName, ticket));
            }
            if (!HasAvailable(result.tracks)) {  // SRC-12
                return ShowStatus(guardedPlayer, tr("Nothing found"));
            }
            guardedPlayer->setQueue(TracksOf(result.tracks), title, true);
        }
    );
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
    QPointer<Player> guardedPlayer(corePlayer);
    yandexLibrary->setLiked(CatalogId(key), liked, [guardedPlayer, liked](bool, const QString& error) {
        if (!guardedPlayer) {
            return;
        }
        QString text;
        if (!error.isEmpty()) {
            text = tr("Error: %1").arg(error);
        } else if (liked) {
            text = tr("Added to Liked");
        } else {
            text = tr("Removed from Liked");
        }
        ShowStatus(guardedPlayer, text);
    });
}

void Catalog::dislikeAndSkip(const Core::TrackKey& key) {
    QPointer<Player> guardedPlayer(corePlayer);
    yandexLibrary->dislike(CatalogId(key), [guardedPlayer](bool, const QString& error) {
        if (!guardedPlayer) {
            return;
        }
        ShowStatus(guardedPlayer, error.isEmpty() ? tr("Disliked") : tr("Error: %1").arg(error));
    });
    if (isCurrent(key)) {
        corePlayer->next();
    }
}

}  // namespace YandexSource
