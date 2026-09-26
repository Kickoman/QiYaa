#include "ui/library_menu.h"

#include "core/player.h"
#include "yandex/api_client.h"
#include "yandex/library.h"

#include <QAction>
#include <QDesktopServices>
#include <QHash>
#include <QInputDialog>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QPointer>

#include <algorithm>
#include <memory>

namespace Ui {

using Yandex::Library;
using Yandex::NamedReference;
using Yandex::PlaylistReference;
using Yandex::Station;
using Yandex::Track;
using Yandex::WaveBatch;

namespace {

void ShowStatus(Core::Player* player, const QString& text) {
    Q_EMIT player->statusMessage(text);
}

auto QueueLoader(Core::Player* player, const QString& title, quint64 ticket = 0) {
    if (ticket == 0) {
        ticket = player->newSourceRequest();
    }
    QPointer<Core::Player> guardedPlayer(player);
    return
        [guardedPlayer, title, ticket](const QList<Yandex::Track>& tracks, const QString& error) {
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

template <typename T>
void LazySubmenu(
    QMenu* submenu,
    std::function<void(Yandex::Library::TCallback<QList<T>>)> load,
    std::function<void(QMenu*, const QList<T>&)> fill
) {
    auto loaded = std::make_shared<bool>(false);
    QObject::connect(submenu, &QMenu::aboutToShow, submenu, [submenu, loaded, load, fill] {
        if (*loaded) {
            return;
        }
        *loaded = true;
        QAction* placeholder = submenu->addAction(QStringLiteral("Загрузка..."));
        placeholder->setEnabled(false);
        QPointer<QMenu> guardedMenu(submenu);
        load([guardedMenu, placeholder, fill](const QList<T>& items, const QString& error) {
            if (!guardedMenu) {
                return;
            }
            guardedMenu->removeAction(placeholder);
            placeholder->deleteLater();
            if (!error.isEmpty()) {
                guardedMenu->addAction(QStringLiteral("Ошибка: ") + error)->setEnabled(false);
                return;
            }
            if (items.isEmpty()) {
                guardedMenu->addAction(QStringLiteral("(пусто)"))->setEnabled(false);
                return;
            }
            fill(guardedMenu, items);
        });
    });
}

QStringList& CurrentWaveSeeds() {
    static QStringList seeds{QStringLiteral("user:onyourwave")};
    return seeds;
}

QString StationTypeTitle(const QString& type) {
    static const QMap<QString, QString> names{
        {QStringLiteral("genre"), QStringLiteral("Жанры")},
        {QStringLiteral("mood"), QStringLiteral("Настроение")},
        {QStringLiteral("activity"), QStringLiteral("Занятия")},
        {QStringLiteral("epoch"), QStringLiteral("Эпохи")},
        {QStringLiteral("local"), QStringLiteral("Местное")},
        {QStringLiteral("author"), QStringLiteral("Авторы")},
        {QStringLiteral("user"), QStringLiteral("Персональные")},
        {QStringLiteral("personal"), QStringLiteral("Персональные")},
    };
    return names.value(type, type);
}

}  // namespace

void PlayWave(Core::Player* player, const QStringList& seeds, const QString& title) {
    Yandex::Library* library = player->library();
    ShowStatus(player, title + QStringLiteral(": загрузка..."));
    QPointer<Core::Player> guardedPlayer(player);
    const quint64 ticket = player->newSourceRequest();
    library->startWave(
        seeds,
        [guardedPlayer, library, title, ticket,
         seeds](const Yandex::WaveBatch& batch, const QString& error) {
            if (!guardedPlayer || !guardedPlayer->isLatestSourceRequest(ticket)) {
                return;
            }
            if (!error.isEmpty()) {
                return ShowStatus(guardedPlayer, QStringLiteral("Ошибка волны: ") + error);
            }
            struct WaveState {
                QString session;
                QString station;
                QHash<QString, QString> batchOfTrack;
            };
            auto state = std::make_shared<WaveState>();
            state->session = batch.sessionId;
            state->station = seeds.value(0);
            for (const Yandex::Track& track : batch.tracks) {
                state->batchOfTrack.insert(track.id, batch.batchId);
            }
            CurrentWaveSeeds() = seeds;

            Core::Player::TLoadMoreCallback more =
                [guardedPlayer, library,
                 state](std::function<void(const QList<Yandex::Track>&)> done) {
                    if (!guardedPlayer) {
                        return;
                    }
                    QStringList queue;
                    const auto& playlist = guardedPlayer->playlist();
                    for (qsizetype i = std::max<qsizetype>(0, playlist.size() - 5);
                         i < playlist.size(); ++i) {
                        queue << playlist[i].id;
                    }
                    library->moreWave(
                        state->session, queue,
                        [done,
                         state](const Yandex::WaveBatch& nextBatch, const QString& waveError) {
                            if (!waveError.isEmpty()) {
                                qWarning("wave: %s", qPrintable(waveError));
                            }
                            for (const Yandex::Track& track : nextBatch.tracks) {
                                state->batchOfTrack.insert(track.id, nextBatch.batchId);
                            }
                            done(nextBatch.tracks);
                        }
                    );
                };
            Core::Player::TEventCallback events =
                [library,
                 state](Core::Player::TrackEvent event, const Yandex::Track& track, double played) {
                    const Yandex::WaveEvent waveEvent = event == Core::Player::TrackEvent::Started
                        ? Yandex::WaveEvent::TrackStarted
                        : event == Core::Player::TrackEvent::Finished
                        ? Yandex::WaveEvent::TrackFinished
                        : Yandex::WaveEvent::Skip;
                    library->waveFeedback(
                        state->session, state->station, state->batchOfTrack.value(track.id),
                        waveEvent, &track, played
                    );
                };
            library->waveFeedback(
                state->session, state->station, batch.batchId, Yandex::WaveEvent::RadioStarted
            );
            guardedPlayer->setQueue(batch.tracks, title, true, more, events);
        }
    );
}

void PlayMyWave(Core::Player* player) {
    PlayWave(player, {QStringLiteral("user:onyourwave")}, QStringLiteral("Моя волна"));
}

void PlayLikes(Core::Player* player, bool autoplay) {
    ShowStatus(player, QStringLiteral("Мне нравится: загрузка..."));
    QPointer<Core::Player> guardedPlayer(player);
    const quint64 ticket = player->newSourceRequest();
    player->library()->likedTracks([guardedPlayer, autoplay, ticket](
                                       const QList<Yandex::Track>& tracks, const QString& error
                                   ) {
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

void PlaySearchResults(Core::Player* player, const QString& text) {
    Yandex::Library* library = player->library();
    const QString title = QStringLiteral("Поиск: ") + text;
    ShowStatus(player, title + QStringLiteral("..."));
    QPointer<Core::Player> guardedPlayer(player);
    const quint64 ticket = player->newSourceRequest();
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

void AddLibraryActions(
    QMenu* menu,
    Core::Player* player,
    QWidget* dialogParent,
    std::function<void()> loginRequested
) {
    Yandex::Library* library = player->library();
    if (!library->isLoggedIn()) {
        menu->addAction(QStringLiteral("Войти в Яндекс Музыку..."), menu, loginRequested);
        return;
    }

    menu->addAction(QStringLiteral("Моя волна"), menu, [player] { Ui::PlayMyWave(player); });
    menu->addAction(QStringLiteral("Мне нравится"), menu, [player] {
        Ui::PlayLikes(player, true);
    });

    QMenu* wheel = menu->addMenu(QStringLiteral("Колесо волн"));
    LazySubmenu<Yandex::Wave>(
        wheel, [library](auto callback) { library->wheelWaves(CurrentWaveSeeds(), callback); },
        [player](QMenu* submenu, const QList<Yandex::Wave>& waves) {
            for (const Yandex::Wave& wave : waves) {
                QAction* action = submenu->addAction(wave.name, submenu, [player, wave] {
                    Ui::PlayWave(player, wave.seeds, wave.name);
                });
                action->setToolTip(wave.description);
            }
            submenu->setToolTipsVisible(true);
        }
    );

    QMenu* forYou = menu->addMenu(QStringLiteral("Для вас"));
    LazySubmenu<Yandex::PlaylistReference>(
        forYou, [library](auto callback) { library->personalPlaylists(callback); },
        [player, library](QMenu* submenu, const QList<Yandex::PlaylistReference>& items) {
            for (const Yandex::PlaylistReference& playlist : items) {
                submenu->addAction(playlist.title, submenu, [player, library, playlist] {
                    library->playlistTracks(playlist, QueueLoader(player, playlist.title));
                });
            }
        }
    );

    QMenu* playlists = menu->addMenu(QStringLiteral("Плейлисты"));
    LazySubmenu<Yandex::PlaylistReference>(
        playlists, [library](auto callback) { library->userPlaylists(callback); },
        [player, library](QMenu* submenu, const QList<Yandex::PlaylistReference>& items) {
            for (const Yandex::PlaylistReference& playlist : items) {
                QMenu* playlistMenu = submenu->addMenu(
                    QStringLiteral("%1 (%2)").arg(playlist.title).arg(playlist.trackCount)
                );
                playlistMenu->addAction(
                    QStringLiteral("Слушать"), playlistMenu,
                    [player, library, playlist] {
                        library->playlistTracks(playlist, QueueLoader(player, playlist.title));
                    }
                );
                playlistMenu->addAction(
                    QStringLiteral("Похожие треки"), playlistMenu,
                    [player, library, playlist] {
                        library->playlistRecommendations(
                            playlist,
                            QueueLoader(player, playlist.title + QStringLiteral(": похожие"))
                        );
                    }
                );
            }
        }
    );

    QMenu* artists = menu->addMenu(QStringLiteral("Исполнители"));
    LazySubmenu<Yandex::NamedReference>(
        artists, [library](auto callback) { library->likedArtists(callback); },
        [player, library](QMenu* submenu, const QList<Yandex::NamedReference>& items) {
            for (const Yandex::NamedReference& artist : items) {
                submenu->addAction(artist.name, submenu, [player, library, artist] {
                    library->artistTopTracks(artist.id, QueueLoader(player, artist.name));
                });
            }
        }
    );

    QMenu* albums = menu->addMenu(QStringLiteral("Альбомы"));
    LazySubmenu<Yandex::NamedReference>(
        albums, [library](auto callback) { library->likedAlbums(callback); },
        [player, library](QMenu* submenu, const QList<Yandex::NamedReference>& items) {
            for (const Yandex::NamedReference& album : items) {
                submenu->addAction(album.name, submenu, [player, library, album] {
                    library->albumTracks(album.id, QueueLoader(player, album.name));
                });
            }
        }
    );

    QMenu* stations = menu->addMenu(QStringLiteral("Станции"));
    LazySubmenu<Yandex::Station>(
        stations, [library](auto callback) { library->stations(callback); },
        [player](QMenu* submenu, const QList<Yandex::Station>& items) {
            QMap<QString, QMenu*> groups;
            for (const Yandex::Station& station : items) {
                QMenu*& group = groups[station.type];
                if (!group) {
                    group = submenu->addMenu(StationTypeTitle(station.type));
                }
                group->addAction(station.name, group, [player, station] {
                    Ui::PlayWave(player, {station.id}, station.name);
                });
            }
        }
    );

    menu->addAction(QStringLiteral("Поиск..."), menu, [player, dialogParent] {
        bool ok = false;
        const QString text = QInputDialog::getText(
            dialogParent, QStringLiteral("Поиск"), QStringLiteral("Исполнитель, альбом или трек:"),
            QLineEdit::Normal, {}, &ok
        );
        if (ok && !text.trimmed().isEmpty()) {
            Ui::PlaySearchResults(player, text.trimmed());
        }
    });

    menu->addSeparator();
    const Yandex::Track* currentTrack = player->currentTrack();
    const QString id = currentTrack ? currentTrack->id : QString();
    const bool liked = currentTrack && library->isLiked(id);
    QAction* like = menu->addAction(
        liked ? QStringLiteral("Убрать из «Мне нравится»") : QStringLiteral("Нравится"), menu,
        [player, library, id, liked] {
            library->setLiked(id, !liked, [player, liked](bool, const QString& error) {
                ShowStatus(
                    player,
                    !error.isEmpty() ? QStringLiteral("Ошибка: ") + error
                        : liked ? QStringLiteral("Убрано из «Мне нравится»")
                                : QStringLiteral("Добавлено в «Мне нравится»")
                );
            });
        }
    );
    QAction* dislike =
        menu->addAction(QStringLiteral("Не нравится (пропустить)"), menu, [player, library, id] {
            library->dislike(id, [player](bool, const QString& error) {
                ShowStatus(
                    player,
                    error.isEmpty() ? QStringLiteral("Дизлайк поставлен")
                                    : QStringLiteral("Ошибка: ") + error
                );
            });
            player->next();
        });
    const QUrl webUrl = currentTrack ? currentTrack->webUrl() : QUrl();
    QAction* open = menu->addAction(QStringLiteral("Открыть трек в браузере"), menu, [webUrl] {
        QDesktopServices::openUrl(webUrl);
    });
    for (QAction* action : {like, dislike, open}) {
        action->setEnabled(currentTrack != nullptr);
    }
}

}  // namespace Ui
