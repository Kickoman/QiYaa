#include "ui/library_menu.h"

#include "core/player.h"

#include <QDesktopServices>
#include <QHash>
#include <QInputDialog>
#include <QMap>
#include <QMenu>
#include <QPointer>

#include <memory>

namespace Ui {

using Yandex::Library;
using Yandex::NamedRef;
using Yandex::PlaylistRef;
using Yandex::Station;
using Yandex::Track;
using Yandex::WaveBatch;

namespace {

void status(Core::Player* player, const QString& text) {
    Q_EMIT player->statusMessage(text);
}

// Loads `tracks` into the player as a finite source. Takes a request ticket
// now, so a slow response can't replace something the user picked later.
auto queueLoader(Core::Player* player, const QString& title, quint64 ticket = 0) {
    if (ticket == 0) {
        ticket = player->newSourceRequest();
    }
    QPointer<Core::Player> p(player);
    return [p, title, ticket](const QList<Yandex::Track>& tracks, const QString& err) {
        if (!p || !p->isLatestSourceRequest(ticket)) {
            return;
        }
        if (!err.isEmpty()) {
            return status(p, QStringLiteral("Ошибка: ") + err);
        }
        if (tracks.isEmpty()) {
            return status(p, title + QStringLiteral(": пусто"));
        }
        p->setQueue(tracks, title, true);
    };
}

// Fills a submenu when it is first shown. `load` fetches items, `fill` turns them into actions.
template <typename T>
void lazySubmenu(
    QMenu* sub,
    std::function<void(Yandex::Library::Callback<QList<T>>)> load,
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
        load([guard, wait, fill](const QList<T>& items, const QString& err) {
            if (!guard) {
                return;
            }
            guard->removeAction(wait);
            wait->deleteLater();
            if (!err.isEmpty()) {
                guard->addAction(QStringLiteral("Ошибка: ") + err)->setEnabled(false);
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

// Seeds of the wave that was started last (the wheel suggests waves around it).
QStringList& currentWaveSeeds() {
    static QStringList seeds{QStringLiteral("user:onyourwave")};
    return seeds;
}

QString stationTypeTitle(const QString& type) {
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

void playWave(Core::Player* player, const QStringList& seeds, const QString& title) {
    Yandex::Library* lib = player->library();
    status(player, title + QStringLiteral(": загрузка..."));
    QPointer<Core::Player> p(player);
    const quint64 ticket = player->newSourceRequest();
    lib->startWave(
        seeds,
        [p, lib, title, ticket, seeds](const Yandex::WaveBatch& batch, const QString& err) {
            if (!p || !p->isLatestSourceRequest(ticket)) {
                return;
            }
            if (!err.isEmpty()) {
                return status(p, QStringLiteral("Ошибка волны: ") + err);
            }
            // Shared by the "more" and feedback callbacks for the life of this queue.
            struct WaveState {
                QString session;
                QString station;
                QHash<QString, QString> batchOfTrack;
            };
            auto state = std::make_shared<WaveState>();
            state->session = batch.sessionId;
            state->station = seeds.value(0);
            for (const Yandex::Track& t : batch.tracks) {
                state->batchOfTrack.insert(t.id, batch.batchId);
            }
            currentWaveSeeds() = seeds;

            // Endless: ask the session for the next batch, seeded with what we've queued.
            Core::Player::MoreFn more = [p, lib, state](
                                            std::function<void(const QList<Yandex::Track>&)> done
                                        ) {
                if (!p) {
                    return;
                }
                QStringList queue;
                const auto& list = p->playlist();
                for (qsizetype i = std::max<qsizetype>(0, list.size() - 5); i < list.size(); ++i) {
                    queue << list[i].id;
                }
                lib->moreWave(
                    state->session, queue,
                    [done, state](const Yandex::WaveBatch& b, const QString& err) {
                        if (!err.isEmpty()) {
                            qWarning("wave: %s", qPrintable(err));
                        }
                        for (const Yandex::Track& t : b.tracks) {
                            state->batchOfTrack.insert(t.id, b.batchId);
                        }
                        done(b.tracks);
                    }
                );
            };
            // Feedback makes the wave adapt: what was played through, what was skipped.
            Core::Player::EventFn events =
                [lib, state](Core::Player::TrackEvent ev, const Yandex::Track& t, double played) {
                    const Yandex::WaveEvent we = ev == Core::Player::TrackEvent::Started
                        ? Yandex::WaveEvent::TrackStarted
                        : ev == Core::Player::TrackEvent::Finished
                        ? Yandex::WaveEvent::TrackFinished
                        : Yandex::WaveEvent::Skip;
                    lib->waveFeedback(
                        state->session, state->station, state->batchOfTrack.value(t.id), we, &t,
                        played
                    );
                };
            lib->waveFeedback(
                state->session, state->station, batch.batchId, Yandex::WaveEvent::RadioStarted
            );
            p->setQueue(batch.tracks, title, true, more, events);
        }
    );
}

void playMyWave(Core::Player* player) {
    playWave(player, {QStringLiteral("user:onyourwave")}, QStringLiteral("Моя волна"));
}

void playLikes(Core::Player* player, bool autoplay) {
    status(player, QStringLiteral("Мне нравится: загрузка..."));
    QPointer<Core::Player> p(player);
    const quint64 ticket = player->newSourceRequest();
    player->library()->likedTracks(
        [p, autoplay, ticket](const QList<Yandex::Track>& tracks, const QString& err) {
            if (!p || !p->isLatestSourceRequest(ticket)) {
                return;
            }
            if (!err.isEmpty()) {
                return status(p, QStringLiteral("Ошибка: ") + err);
            }
            p->setQueue(tracks, QStringLiteral("Мне нравится"), autoplay);
            status(p, QStringLiteral("Мне нравится: %1 треков").arg(p->playlist().size()));
        }
    );
}

void playSearchResults(Core::Player* player, const QString& text) {
    Yandex::Library* lib = player->library();
    const QString title = QStringLiteral("Поиск: ") + text;
    status(player, title + QStringLiteral("..."));
    QPointer<Core::Player> p(player);
    const quint64 ticket = player->newSourceRequest();
    lib->search(text, [p, lib, title, ticket](const Yandex::SearchResult& r, const QString& err) {
        if (!p || !p->isLatestSourceRequest(ticket)) {
            return;
        }
        if (!err.isEmpty()) {
            return status(p, QStringLiteral("Ошибка поиска: ") + err);
        }
        if (r.bestType == QLatin1String("artist") && !r.bestId.isEmpty()) {
            return lib->artistTopTracks(r.bestId, queueLoader(p, r.bestName, ticket));
        }
        if (r.bestType == QLatin1String("album") && !r.bestId.isEmpty()) {
            return lib->albumTracks(r.bestId, queueLoader(p, r.bestName, ticket));
        }
        if (r.tracks.isEmpty()) {
            return status(p, QStringLiteral("Ничего не найдено"));
        }
        p->setQueue(r.tracks, title, true);
    });
}

void addLibraryActions(
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

    menu->addAction(QStringLiteral("Моя волна"), menu, [player] { Ui::playMyWave(player); });
    menu->addAction(QStringLiteral("Мне нравится"), menu, [player] {
        Ui::playLikes(player, true);
    });

    QMenu* wheel = menu->addMenu(QStringLiteral("Колесо волн"));
    lazySubmenu<Yandex::Wave>(
        wheel, [lib](auto cb) { lib->wheelWaves(currentWaveSeeds(), cb); },
        [player](QMenu* m, const QList<Yandex::Wave>& waves) {
            for (const Yandex::Wave& w : waves) {
                QAction* a =
                    m->addAction(w.name, m, [player, w] { Ui::playWave(player, w.seeds, w.name); });
                a->setToolTip(w.description);
            }
            m->setToolTipsVisible(true);
        }
    );

    QMenu* forYou = menu->addMenu(QStringLiteral("Для вас"));
    lazySubmenu<Yandex::PlaylistRef>(
        forYou, [lib](auto cb) { lib->personalPlaylists(cb); },
        [player, lib](QMenu* m, const QList<Yandex::PlaylistRef>& items) {
            for (const Yandex::PlaylistRef& pl : items) {
                m->addAction(pl.title, m, [player, lib, pl] {
                    lib->playlistTracks(pl, queueLoader(player, pl.title));
                });
            }
        }
    );

    QMenu* playlists = menu->addMenu(QStringLiteral("Плейлисты"));
    lazySubmenu<Yandex::PlaylistRef>(
        playlists, [lib](auto cb) { lib->userPlaylists(cb); },
        [player, lib](QMenu* m, const QList<Yandex::PlaylistRef>& items) {
            for (const Yandex::PlaylistRef& pl : items) {
                QMenu* one = m->addMenu(QStringLiteral("%1 (%2)").arg(pl.title).arg(pl.trackCount));
                one->addAction(QStringLiteral("Слушать"), one, [player, lib, pl] {
                    lib->playlistTracks(pl, queueLoader(player, pl.title));
                });
                one->addAction(QStringLiteral("Похожие треки"), one, [player, lib, pl] {
                    lib->playlistRecommendations(
                        pl, queueLoader(player, pl.title + QStringLiteral(": похожие"))
                    );
                });
            }
        }
    );

    QMenu* artists = menu->addMenu(QStringLiteral("Исполнители"));
    lazySubmenu<Yandex::NamedRef>(
        artists, [lib](auto cb) { lib->likedArtists(cb); },
        [player, lib](QMenu* m, const QList<Yandex::NamedRef>& items) {
            for (const Yandex::NamedRef& a : items) {
                m->addAction(a.name, m, [player, lib, a] {
                    lib->artistTopTracks(a.id, queueLoader(player, a.name));
                });
            }
        }
    );

    QMenu* albums = menu->addMenu(QStringLiteral("Альбомы"));
    lazySubmenu<Yandex::NamedRef>(
        albums, [lib](auto cb) { lib->likedAlbums(cb); },
        [player, lib](QMenu* m, const QList<Yandex::NamedRef>& items) {
            for (const Yandex::NamedRef& a : items) {
                m->addAction(a.name, m, [player, lib, a] {
                    lib->albumTracks(a.id, queueLoader(player, a.name));
                });
            }
        }
    );

    QMenu* stations = menu->addMenu(QStringLiteral("Станции"));
    lazySubmenu<Yandex::Station>(
        stations, [lib](auto cb) { lib->stations(cb); },
        [player](QMenu* m, const QList<Yandex::Station>& items) {
            QMap<QString, QMenu*> groups;
            for (const Yandex::Station& s : items) {
                QMenu*& g = groups[s.type];
                if (!g) {
                    g = m->addMenu(stationTypeTitle(s.type));
                }
                g->addAction(s.name, g, [player, s] { Ui::playWave(player, {s.id}, s.name); });
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
            Ui::playSearchResults(player, text.trimmed());
        }
    });

    // Current track.
    menu->addSeparator();
    const Yandex::Track* cur = player->currentTrack();
    const QString id = cur ? cur->id : QString();
    const bool liked = cur && lib->isLiked(id);
    QAction* like = menu->addAction(
        liked ? QStringLiteral("Убрать из «Мне нравится»") : QStringLiteral("Нравится"), menu,
        [player, lib, id, liked] {
            lib->setLiked(id, !liked, [player, liked](bool, const QString& err) {
                status(
                    player,
                    !err.isEmpty() ? QStringLiteral("Ошибка: ") + err
                        : liked    ? QStringLiteral("Убрано из «Мне нравится»")
                                : QStringLiteral("Добавлено в «Мне нравится»")
                );
            });
        }
    );
    QAction* dislike =
        menu->addAction(QStringLiteral("Не нравится (пропустить)"), menu, [player, lib, id] {
            lib->dislike(id, [player](bool, const QString& err) {
                status(
                    player,
                    err.isEmpty() ? QStringLiteral("Дизлайк поставлен")
                                  : QStringLiteral("Ошибка: ") + err
                );
            });
            player->next();
        });
    const QUrl web = cur ? cur->webUrl() : QUrl();
    QAction* open = menu->addAction(QStringLiteral("Открыть трек в браузере"), menu, [web] {
        QDesktopServices::openUrl(web);
    });
    for (QAction* a : {like, dislike, open}) {
        a->setEnabled(cur != nullptr);
    }
}

}  // namespace Ui
