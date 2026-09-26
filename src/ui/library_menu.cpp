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
using Yandex::NamedRef;
using Yandex::PlaylistRef;
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
    QMenu* sub,
    std::function<void(Yandex::Library::TCallback<QList<T>>)> load,
    std::function<void(QMenu*, const QList<T>&)> fill
) {
    auto loaded = std::make_shared<bool>(false);
    QObject::connect(sub, &QMenu::aboutToShow, sub, [sub, loaded, load, fill] {
        if (*loaded) {
            return;
        }
        *loaded = true;
        QAction* wait = sub->addAction(QStringLiteral("Загрузка..."));
        wait->setEnabled(false);
        QPointer<QMenu> guard(sub);
        load([guard, wait, fill](const QList<T>& items, const QString& error) {
            if (!guard) {
                return;
            }
            guard->removeAction(wait);
            wait->deleteLater();
            if (!error.isEmpty()) {
                guard->addAction(QStringLiteral("Ошибка: ") + error)->setEnabled(false);
                return;
            }
            if (items.isEmpty()) {
                guard->addAction(QStringLiteral("(пусто)"))->setEnabled(false);
                return;
            }
            fill(guard, items);
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
    Yandex::Library* lib = player->library();
    ShowStatus(player, title + QStringLiteral(": загрузка..."));
    QPointer<Core::Player> guardedPlayer(player);
    const quint64 ticket = player->newSourceRequest();
    lib->startWave(
        seeds,
        [guardedPlayer, lib, title, ticket,
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

            Core::Player::TMoreFn more = [guardedPlayer, lib, state](
                                             std::function<void(const QList<Yandex::Track>&)> done
                                         ) {
                if (!guardedPlayer) {
                    return;
                }
                QStringList queue;
                const auto& list = guardedPlayer->playlist();
                for (qsizetype i = std::max<qsizetype>(0, list.size() - 5); i < list.size(); ++i) {
                    queue << list[i].id;
                }
                lib->moreWave(
                    state->session, queue,
                    [done, state](const Yandex::WaveBatch& nextBatch, const QString& waveError) {
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
            Core::Player::TEventFn events =
                [lib,
                 state](Core::Player::TrackEvent ev, const Yandex::Track& track, double played) {
                    const Yandex::WaveEvent we = ev == Core::Player::TrackEvent::Started
                        ? Yandex::WaveEvent::TrackStarted
                        : ev == Core::Player::TrackEvent::Finished
                        ? Yandex::WaveEvent::TrackFinished
                        : Yandex::WaveEvent::Skip;
                    lib->waveFeedback(
                        state->session, state->station, state->batchOfTrack.value(track.id), we,
                        &track, played
                    );
                };
            lib->waveFeedback(
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
    Yandex::Library* lib = player->library();
    const QString title = QStringLiteral("Поиск: ") + text;
    ShowStatus(player, title + QStringLiteral("..."));
    QPointer<Core::Player> guardedPlayer(player);
    const quint64 ticket = player->newSourceRequest();
    lib->search(
        text,
        [guardedPlayer, lib, title, ticket](const Yandex::SearchResult& r, const QString& error) {
            if (!guardedPlayer || !guardedPlayer->isLatestSourceRequest(ticket)) {
                return;
            }
            if (!error.isEmpty()) {
                return ShowStatus(guardedPlayer, QStringLiteral("Ошибка поиска: ") + error);
            }
            if (r.bestKind == Yandex::SearchResult::Kind::Artist && !r.bestId.isEmpty()) {
                return lib->artistTopTracks(
                    r.bestId, QueueLoader(guardedPlayer, r.bestName, ticket)
                );
            }
            if (r.bestKind == Yandex::SearchResult::Kind::Album && !r.bestId.isEmpty()) {
                return lib->albumTracks(r.bestId, QueueLoader(guardedPlayer, r.bestName, ticket));
            }
            if (r.tracks.isEmpty()) {
                return ShowStatus(guardedPlayer, QStringLiteral("Ничего не найдено"));
            }
            guardedPlayer->setQueue(r.tracks, title, true);
        }
    );
}

void AddLibraryActions(
    QMenu* menu,
    Core::Player* player,
    QWidget* dialogParent,
    std::function<void()> loginRequested
) {
    Yandex::Library* lib = player->library();
    if (!lib->isLoggedIn()) {
        menu->addAction(QStringLiteral("Войти в Яндекс Музыку..."), menu, loginRequested);
        return;
    }

    menu->addAction(QStringLiteral("Моя волна"), menu, [player] { Ui::PlayMyWave(player); });
    menu->addAction(QStringLiteral("Мне нравится"), menu, [player] {
        Ui::PlayLikes(player, true);
    });

    QMenu* wheel = menu->addMenu(QStringLiteral("Колесо волн"));
    LazySubmenu<Yandex::Wave>(
        wheel, [lib](auto callback) { lib->wheelWaves(CurrentWaveSeeds(), callback); },
        [player](QMenu* submenu, const QList<Yandex::Wave>& waves) {
            for (const Yandex::Wave& w : waves) {
                QAction* action = submenu->addAction(w.name, submenu, [player, w] {
                    Ui::PlayWave(player, w.seeds, w.name);
                });
                action->setToolTip(w.description);
            }
            submenu->setToolTipsVisible(true);
        }
    );

    QMenu* forYou = menu->addMenu(QStringLiteral("Для вас"));
    LazySubmenu<Yandex::PlaylistRef>(
        forYou, [lib](auto callback) { lib->personalPlaylists(callback); },
        [player, lib](QMenu* submenu, const QList<Yandex::PlaylistRef>& items) {
            for (const Yandex::PlaylistRef& playlist : items) {
                submenu->addAction(playlist.title, submenu, [player, lib, playlist] {
                    lib->playlistTracks(playlist, QueueLoader(player, playlist.title));
                });
            }
        }
    );

    QMenu* playlists = menu->addMenu(QStringLiteral("Плейлисты"));
    LazySubmenu<Yandex::PlaylistRef>(
        playlists, [lib](auto callback) { lib->userPlaylists(callback); },
        [player, lib](QMenu* submenu, const QList<Yandex::PlaylistRef>& items) {
            for (const Yandex::PlaylistRef& playlist : items) {
                QMenu* one = submenu->addMenu(
                    QStringLiteral("%1 (%2)").arg(playlist.title).arg(playlist.trackCount)
                );
                one->addAction(QStringLiteral("Слушать"), one, [player, lib, playlist] {
                    lib->playlistTracks(playlist, QueueLoader(player, playlist.title));
                });
                one->addAction(QStringLiteral("Похожие треки"), one, [player, lib, playlist] {
                    lib->playlistRecommendations(
                        playlist, QueueLoader(player, playlist.title + QStringLiteral(": похожие"))
                    );
                });
            }
        }
    );

    QMenu* artists = menu->addMenu(QStringLiteral("Исполнители"));
    LazySubmenu<Yandex::NamedRef>(
        artists, [lib](auto callback) { lib->likedArtists(callback); },
        [player, lib](QMenu* submenu, const QList<Yandex::NamedRef>& items) {
            for (const Yandex::NamedRef& a : items) {
                submenu->addAction(a.name, submenu, [player, lib, a] {
                    lib->artistTopTracks(a.id, QueueLoader(player, a.name));
                });
            }
        }
    );

    QMenu* albums = menu->addMenu(QStringLiteral("Альбомы"));
    LazySubmenu<Yandex::NamedRef>(
        albums, [lib](auto callback) { lib->likedAlbums(callback); },
        [player, lib](QMenu* submenu, const QList<Yandex::NamedRef>& items) {
            for (const Yandex::NamedRef& a : items) {
                submenu->addAction(a.name, submenu, [player, lib, a] {
                    lib->albumTracks(a.id, QueueLoader(player, a.name));
                });
            }
        }
    );

    QMenu* stations = menu->addMenu(QStringLiteral("Станции"));
    LazySubmenu<Yandex::Station>(
        stations, [lib](auto callback) { lib->stations(callback); },
        [player](QMenu* submenu, const QList<Yandex::Station>& items) {
            QMap<QString, QMenu*> groups;
            for (const Yandex::Station& s : items) {
                QMenu*& g = groups[s.type];
                if (!g) {
                    g = submenu->addMenu(StationTypeTitle(s.type));
                }
                g->addAction(s.name, g, [player, s] { Ui::PlayWave(player, {s.id}, s.name); });
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
    const Yandex::Track* cur = player->currentTrack();
    const QString id = cur ? cur->id : QString();
    const bool liked = cur && lib->isLiked(id);
    QAction* like = menu->addAction(
        liked ? QStringLiteral("Убрать из «Мне нравится»") : QStringLiteral("Нравится"), menu,
        [player, lib, id, liked] {
            lib->setLiked(id, !liked, [player, liked](bool, const QString& error) {
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
        menu->addAction(QStringLiteral("Не нравится (пропустить)"), menu, [player, lib, id] {
            lib->dislike(id, [player](bool, const QString& error) {
                ShowStatus(
                    player,
                    error.isEmpty() ? QStringLiteral("Дизлайк поставлен")
                                    : QStringLiteral("Ошибка: ") + error
                );
            });
            player->next();
        });
    const QUrl web = cur ? cur->webUrl() : QUrl();
    QAction* open = menu->addAction(QStringLiteral("Открыть трек в браузере"), menu, [web] {
        QDesktopServices::openUrl(web);
    });
    for (QAction* action : {like, dislike, open}) {
        action->setEnabled(cur != nullptr);
    }
}

}  // namespace Ui
