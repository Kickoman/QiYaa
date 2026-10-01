#include "core/sources.h"

#include "core/player.h"
#include "yandex/api_client.h"
#include "yandex/library.h"

#include <QHash>
#include <QList>
#include <QPointer>

#include <algorithm>
#include <functional>
#include <memory>

namespace Core {

using Yandex::Track;

namespace {

constexpr qsizetype kWaveHistory = 5;

void ShowStatus(Player* player, const QString& text) {
    Q_EMIT player->statusMessage(text);
}

// The callback for a request that loads a source's tracks: applies them if `ticket` is still the
// latest pick.
auto QueueLoader(Player* player, const QString& title, quint64 ticket) {
    QPointer<Player> guardedPlayer(player);
    return [guardedPlayer, title, ticket](const QList<Track>& tracks, const QString& error) {
        if (!guardedPlayer || !guardedPlayer->isLatestSourceRequest(ticket)) {
            return;
        }
        if (!error.isEmpty()) {
            return ShowStatus(guardedPlayer, QStringLiteral("Ошибка: ") + error);
        }
        if (tracks.isEmpty()) {
            return ShowStatus(guardedPlayer, title + QStringLiteral(": пусто"));
        }
        guardedPlayer->setQueue(tracks, title, true);
    };
}

// What a wave remembers for its feedback: the session, the station (the first seed) and which
// batch each track came in.
struct WaveState {
    QString session;
    QString station;
    QHash<QString, QString> batchOfTrack;
};

// The wave's callbacks hold the Library and the Player, not the Sources: the Player closes the
// open track, and sends its feedback, while the application shuts down.
Player::TLoadMoreCallback
WaveMore(QPointer<Player> player, Yandex::Library* library, std::shared_ptr<WaveState> state) {
    return [player, library, state](std::function<void(const QList<Track>&)> done) {
        if (!player) {
            return;
        }
        QStringList queue;
        const QList<Track>& playlist = player->playlist();
        for (qsizetype i = std::max<qsizetype>(0, playlist.size() - kWaveHistory);
             i < playlist.size(); ++i) {
            queue << playlist[i].id;
        }
        library->moreWave(
            state->session, queue,
            [done, state](const Yandex::WaveBatch& nextBatch, const QString& waveError) {
                if (!waveError.isEmpty()) {
                    qWarning("wave: %s", qPrintable(waveError));
                }
                for (const Track& track : nextBatch.tracks) {
                    state->batchOfTrack.insert(track.id, nextBatch.batchId);
                }
                done(nextBatch.tracks);
            }
        );
    };
}

Player::TEventCallback WaveEvents(Yandex::Library* library, std::shared_ptr<WaveState> state) {
    return [library, state](Player::TrackEvent event, const Track& track, double played) {
        const Yandex::WaveEvent waveEvent = event == Player::TrackEvent::Started
            ? Yandex::WaveEvent::TrackStarted
            : event == Player::TrackEvent::Finished ? Yandex::WaveEvent::TrackFinished
                                                    : Yandex::WaveEvent::Skip;
        library->waveFeedback(
            state->session, state->station, state->batchOfTrack.value(track.id), waveEvent, &track,
            played
        );
    };
}

}  // namespace

Sources::Sources(Player* player, Yandex::Library* library, QObject* parent)
    : QObject(parent)
    , corePlayer(player)
    , yandexLibrary(library) { }

void Sources::showStatus(const QString& text) {
    ShowStatus(corePlayer, text);
}

void Sources::playLikes(bool autoplay) {
    showStatus(QStringLiteral("Мне нравится: загрузка..."));
    QPointer<Player> guardedPlayer(corePlayer);
    const quint64 ticket = corePlayer->newSourceRequest();
    yandexLibrary->likedTracks([guardedPlayer, autoplay,
                                ticket](const QList<Track>& tracks, const QString& error) {
        if (!guardedPlayer || !guardedPlayer->isLatestSourceRequest(ticket)) {
            return;
        }
        if (!error.isEmpty()) {
            return ShowStatus(guardedPlayer, QStringLiteral("Ошибка: ") + error);
        }
        guardedPlayer->setQueue(tracks, QStringLiteral("Мне нравится"), autoplay);
        ShowStatus(
            guardedPlayer,
            QStringLiteral("Мне нравится: %1 треков").arg(guardedPlayer->playlist().size())
        );
    });
}

void Sources::playPlaylist(const Yandex::PlaylistReference& playlist) {
    yandexLibrary->playlistTracks(
        playlist, QueueLoader(corePlayer, playlist.title, corePlayer->newSourceRequest())
    );
}

void Sources::playRecommendations(const Yandex::PlaylistReference& playlist) {
    yandexLibrary->playlistRecommendations(
        playlist,
        QueueLoader(
            corePlayer, playlist.title + QStringLiteral(": похожие"), corePlayer->newSourceRequest()
        )
    );
}

void Sources::playArtist(const QString& artistId, const QString& name) {
    yandexLibrary->artistTopTracks(
        artistId, QueueLoader(corePlayer, name, corePlayer->newSourceRequest())
    );
}

void Sources::playAlbum(const QString& albumId, const QString& title) {
    yandexLibrary->albumTracks(
        albumId, QueueLoader(corePlayer, title, corePlayer->newSourceRequest())
    );
}

void Sources::playWave(const QStringList& seeds, const QString& title) {
    Yandex::Library* library = yandexLibrary;
    showStatus(title + QStringLiteral(": загрузка..."));
    QPointer<Player> guardedPlayer(corePlayer);
    QPointer<Sources> self(this);
    const quint64 ticket = corePlayer->newSourceRequest();
    library->startWave(
        seeds,
        [self, guardedPlayer, library, title, ticket,
         seeds](const Yandex::WaveBatch& batch, const QString& error) {
            if (!guardedPlayer || !guardedPlayer->isLatestSourceRequest(ticket)) {
                return;
            }
            if (!error.isEmpty()) {
                return ShowStatus(guardedPlayer, QStringLiteral("Ошибка волны: ") + error);
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
            library->waveFeedback(
                state->session, state->station, batch.batchId, Yandex::WaveEvent::RadioStarted
            );
            guardedPlayer->setQueue(
                batch.tracks, title, true, WaveMore(guardedPlayer, library, state),
                WaveEvents(library, state)
            );
        }
    );
}

void Sources::playMyWave() {
    playWave({kMyWaveSeed}, QStringLiteral("Моя волна"));
}

void Sources::search(const QString& text) {
    Yandex::Library* library = yandexLibrary;
    const QString title = QStringLiteral("Поиск: ") + text;
    showStatus(title + QStringLiteral("..."));
    QPointer<Player> guardedPlayer(corePlayer);
    const quint64 ticket = corePlayer->newSourceRequest();
    library->search(
        text,
        [guardedPlayer, library, title,
         ticket](const Yandex::SearchResult& result, const QString& error) {
            if (!guardedPlayer || !guardedPlayer->isLatestSourceRequest(ticket)) {
                return;
            }
            if (!error.isEmpty()) {
                return ShowStatus(guardedPlayer, QStringLiteral("Ошибка поиска: ") + error);
            }
            // The second request belongs to the same pick: a newer one drops it too (SRC-03).
            if (result.bestKind == Yandex::SearchResult::Kind::Artist && !result.bestId.isEmpty()) {
                return library->artistTopTracks(
                    result.bestId, QueueLoader(guardedPlayer, result.bestName, ticket)
                );
            }
            if (result.bestKind == Yandex::SearchResult::Kind::Album && !result.bestId.isEmpty()) {
                return library->albumTracks(
                    result.bestId, QueueLoader(guardedPlayer, result.bestName, ticket)
                );
            }
            if (result.tracks.isEmpty()) {
                return ShowStatus(guardedPlayer, QStringLiteral("Ничего не найдено"));
            }
            guardedPlayer->setQueue(result.tracks, title, true);
        }
    );
}

void Sources::setLiked(const QString& trackId, bool liked) {
    QPointer<Player> guardedPlayer(corePlayer);
    yandexLibrary->setLiked(trackId, liked, [guardedPlayer, liked](bool, const QString& error) {
        if (!guardedPlayer) {
            return;
        }
        ShowStatus(
            guardedPlayer,
            !error.isEmpty() ? QStringLiteral("Ошибка: ") + error
                : liked      ? QStringLiteral("Добавлено в «Мне нравится»")
                             : QStringLiteral("Убрано из «Мне нравится»")
        );
    });
}

void Sources::dislikeAndSkip(const QString& trackId) {
    QPointer<Player> guardedPlayer(corePlayer);
    yandexLibrary->dislike(trackId, [guardedPlayer](bool, const QString& error) {
        if (!guardedPlayer) {
            return;
        }
        ShowStatus(
            guardedPlayer,
            error.isEmpty() ? QStringLiteral("Дизлайк поставлен")
                            : QStringLiteral("Ошибка: ") + error
        );
    });
    corePlayer->next();
}

}  // namespace Core
