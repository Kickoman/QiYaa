#include "ui/LibraryMenu.h"

#include <memory>

#include <QDesktopServices>
#include <QHash>
#include <QInputDialog>
#include <QMap>
#include <QMenu>
#include <QPointer>

#include "core/Player.h"

namespace qiyaa {

using yandex::Library;
using yandex::NamedRef;
using yandex::PlaylistRef;
using yandex::Station;
using yandex::Track;
using yandex::WaveBatch;

namespace {

void status(Player* player, const QString& text) {
    Q_EMIT player->statusMessage(text);
}

// Loads `tracks` into the player as a finite source. Takes a request ticket
// now, so a slow response can't replace something the user picked later.
auto queueLoader(Player* player, const QString& title, quint64 ticket = 0) {
    if (ticket == 0) ticket = player->newSourceRequest();
    QPointer<Player> p(player);
    return [p, title, ticket](const QList<Track>& tracks, const QString& err) {
        if (!p || !p->isLatestSourceRequest(ticket)) return;
        if (!err.isEmpty()) return status(p, QStringLiteral("Ошибка: ") + err);
        if (tracks.isEmpty()) return status(p, title + QStringLiteral(": пусто"));
        p->setQueue(tracks, title, true);
    };
}

// Fills a submenu when it is first shown. `load` fetches items, `fill` turns them into actions.
template <typename T>
void lazySubmenu(QMenu* sub, std::function<void(Library::Callback<QList<T>>)> load,
                 std::function<void(QMenu*, const QList<T>&)> fill) {
    auto loaded = std::make_shared<bool>(false);
    QObject::connect(sub, &QMenu::aboutToShow, sub, [sub, loaded, load, fill] {
        if (*loaded) return;
        *loaded = true;
        QAction* wait = sub->addAction(QStringLiteral("Загрузка..."));
        wait->setEnabled(false);
        QPointer<QMenu> guard(sub);
        load([guard, wait, fill](const QList<T>& items, const QString& err) {
            if (!guard) return;
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
        {QStringLiteral("genre"), QStringLiteral("Жанры")},       {QStringLiteral("mood"), QStringLiteral("Настроение")},
        {QStringLiteral("activity"), QStringLiteral("Занятия")},  {QStringLiteral("epoch"), QStringLiteral("Эпохи")},
        {QStringLiteral("local"), QStringLiteral("Местное")},     {QStringLiteral("author"), QStringLiteral("Авторы")},
        {QStringLiteral("user"), QStringLiteral("Персональные")}, {QStringLiteral("personal"), QStringLiteral("Персональные")},
    };
    return names.value(type, type);
}

}  // namespace

namespace sources {

void playWave(Player* player, const QStringList& seeds, const QString& title) {
    Library* lib = player->library();
    status(player, title + QStringLiteral(": загрузка..."));
    QPointer<Player> p(player);
    const quint64 ticket = player->newSourceRequest();
    lib->startWave(seeds, [p, lib, title, ticket, seeds](const WaveBatch& batch, const QString& err) {
        if (!p || !p->isLatestSourceRequest(ticket)) return;
        if (!err.isEmpty()) return status(p, QStringLiteral("Ошибка волны: ") + err);
        // Shared by the "more" and feedback callbacks for the life of this queue.
        struct WaveState {
            QString session;
            QString station;
            QHash<QString, QString> batchOfTrack;
        };
        auto state = std::make_shared<WaveState>();
        state->session = batch.sessionId;
        state->station = seeds.value(0);
        for (const Track& t : batch.tracks) state->batchOfTrack.insert(t.id, batch.batchId);
        currentWaveSeeds() = seeds;

        // Endless: ask the session for the next batch, seeded with what we've queued.
        Player::MoreFn more = [p, lib, state](std::function<void(const QList<Track>&)> done) {
            if (!p) return;
            QStringList queue;
            const auto& list = p->playlist();
            for (qsizetype i = std::max<qsizetype>(0, list.size() - 5); i < list.size(); ++i) queue << list[i].id;
            lib->moreWave(state->session, queue, [done, state](const WaveBatch& b, const QString& err) {
                if (!err.isEmpty()) qWarning("wave: %s", qPrintable(err));
                for (const Track& t : b.tracks) state->batchOfTrack.insert(t.id, b.batchId);
                done(b.tracks);
            });
        };
        // Feedback makes the wave adapt: what was played through, what was skipped.
        Player::EventFn events = [lib, state](Player::TrackEvent ev, const Track& t, double played) {
            const yandex::WaveEvent we = ev == Player::TrackEvent::Started    ? yandex::WaveEvent::TrackStarted
                                         : ev == Player::TrackEvent::Finished ? yandex::WaveEvent::TrackFinished
                                                                              : yandex::WaveEvent::Skip;
            lib->waveFeedback(state->session, state->station, state->batchOfTrack.value(t.id), we, &t, played);
        };
        lib->waveFeedback(state->session, state->station, batch.batchId, yandex::WaveEvent::RadioStarted);
        p->setQueue(batch.tracks, title, true, more, events);
    });
}

void playMyWave(Player* player) {
    playWave(player, {QStringLiteral("user:onyourwave")}, QStringLiteral("Моя волна"));
}

void playLikes(Player* player, bool autoplay) {
    status(player, QStringLiteral("Мне нравится: загрузка..."));
    QPointer<Player> p(player);
    const quint64 ticket = player->newSourceRequest();
    player->library()->likedTracks([p, autoplay, ticket](const QList<Track>& tracks, const QString& err) {
        if (!p || !p->isLatestSourceRequest(ticket)) return;
        if (!err.isEmpty()) return status(p, QStringLiteral("Ошибка: ") + err);
        p->setQueue(tracks, QStringLiteral("Мне нравится"), autoplay);
        status(p, QStringLiteral("Мне нравится: %1 треков").arg(p->playlist().size()));
    });
}

void search(Player* player, const QString& text) {
    Library* lib = player->library();
    const QString title = QStringLiteral("Поиск: ") + text;
    status(player, title + QStringLiteral("..."));
    QPointer<Player> p(player);
    const quint64 ticket = player->newSourceRequest();
    lib->search(text, [p, lib, title, ticket](const yandex::SearchResult& r, const QString& err) {
        if (!p || !p->isLatestSourceRequest(ticket)) return;
        if (!err.isEmpty()) return status(p, QStringLiteral("Ошибка поиска: ") + err);
        if (r.bestType == QLatin1String("artist") && !r.bestId.isEmpty())
            return lib->artistTopTracks(r.bestId, queueLoader(p, r.bestName, ticket));
        if (r.bestType == QLatin1String("album") && !r.bestId.isEmpty())
            return lib->albumTracks(r.bestId, queueLoader(p, r.bestName, ticket));
        if (r.tracks.isEmpty()) return status(p, QStringLiteral("Ничего не найдено"));
        p->setQueue(r.tracks, title, true);
    });
}

}  // namespace sources

void addLibraryActions(QMenu* menu, Player* player, QWidget* dialogParent, std::function<void()> loginRequested) {
    Library* lib = player->library();
    if (!lib->isLoggedIn()) {
        menu->addAction(QStringLiteral("Войти в Яндекс Музыку..."), menu, loginRequested);
        return;
    }

    menu->addAction(QStringLiteral("Моя волна"), menu, [player] { sources::playMyWave(player); });
    menu->addAction(QStringLiteral("Мне нравится"), menu, [player] { sources::playLikes(player, true); });

    QMenu* wheel = menu->addMenu(QStringLiteral("Колесо волн"));
    lazySubmenu<yandex::Wave>(
        wheel, [lib](auto cb) { lib->wheelWaves(currentWaveSeeds(), cb); },
        [player](QMenu* m, const QList<yandex::Wave>& waves) {
            for (const yandex::Wave& w : waves) {
                QAction* a = m->addAction(w.name, m, [player, w] { sources::playWave(player, w.seeds, w.name); });
                a->setToolTip(w.description);
            }
            m->setToolTipsVisible(true);
        });

    QMenu* forYou = menu->addMenu(QStringLiteral("Для вас"));
    lazySubmenu<PlaylistRef>(
        forYou, [lib](auto cb) { lib->personalPlaylists(cb); },
        [player, lib](QMenu* m, const QList<PlaylistRef>& items) {
            for (const PlaylistRef& pl : items)
                m->addAction(pl.title, m, [player, lib, pl] { lib->playlistTracks(pl, queueLoader(player, pl.title)); });
        });

    QMenu* playlists = menu->addMenu(QStringLiteral("Плейлисты"));
    lazySubmenu<PlaylistRef>(
        playlists, [lib](auto cb) { lib->userPlaylists(cb); },
        [player, lib](QMenu* m, const QList<PlaylistRef>& items) {
            for (const PlaylistRef& pl : items) {
                QMenu* one = m->addMenu(QStringLiteral("%1 (%2)").arg(pl.title).arg(pl.trackCount));
                one->addAction(QStringLiteral("Слушать"), one,
                               [player, lib, pl] { lib->playlistTracks(pl, queueLoader(player, pl.title)); });
                one->addAction(QStringLiteral("Похожие треки"), one, [player, lib, pl] {
                    lib->playlistRecommendations(pl, queueLoader(player, pl.title + QStringLiteral(": похожие")));
                });
            }
        });

    QMenu* artists = menu->addMenu(QStringLiteral("Исполнители"));
    lazySubmenu<NamedRef>(
        artists, [lib](auto cb) { lib->likedArtists(cb); },
        [player, lib](QMenu* m, const QList<NamedRef>& items) {
            for (const NamedRef& a : items)
                m->addAction(a.name, m, [player, lib, a] { lib->artistTopTracks(a.id, queueLoader(player, a.name)); });
        });

    QMenu* albums = menu->addMenu(QStringLiteral("Альбомы"));
    lazySubmenu<NamedRef>(
        albums, [lib](auto cb) { lib->likedAlbums(cb); },
        [player, lib](QMenu* m, const QList<NamedRef>& items) {
            for (const NamedRef& a : items)
                m->addAction(a.name, m, [player, lib, a] { lib->albumTracks(a.id, queueLoader(player, a.name)); });
        });

    QMenu* stations = menu->addMenu(QStringLiteral("Станции"));
    lazySubmenu<Station>(
        stations, [lib](auto cb) { lib->stations(cb); },
        [player](QMenu* m, const QList<Station>& items) {
            QMap<QString, QMenu*> groups;
            for (const Station& s : items) {
                QMenu*& g = groups[s.type];
                if (!g) g = m->addMenu(stationTypeTitle(s.type));
                g->addAction(s.name, g, [player, s] { sources::playWave(player, {s.id}, s.name); });
            }
        });

    menu->addAction(QStringLiteral("Поиск..."), menu, [player, dialogParent] {
        bool ok = false;
        const QString text = QInputDialog::getText(dialogParent, QStringLiteral("Поиск"),
                                                   QStringLiteral("Исполнитель, альбом или трек:"), QLineEdit::Normal, {}, &ok);
        if (ok && !text.trimmed().isEmpty()) sources::search(player, text.trimmed());
    });

    // Current track.
    menu->addSeparator();
    const Track* cur = player->currentTrack();
    const QString id = cur ? cur->id : QString();
    const bool liked = cur && lib->isLiked(id);
    QAction* like = menu->addAction(liked ? QStringLiteral("Убрать из «Мне нравится»") : QStringLiteral("Нравится"), menu,
                                    [player, lib, id, liked] {
                                        lib->setLiked(id, !liked, [player, liked](bool, const QString& err) {
                                            status(player, !err.isEmpty() ? QStringLiteral("Ошибка: ") + err
                                                           : liked       ? QStringLiteral("Убрано из «Мне нравится»")
                                                                         : QStringLiteral("Добавлено в «Мне нравится»"));
                                        });
                                    });
    QAction* dislike = menu->addAction(QStringLiteral("Не нравится (пропустить)"), menu, [player, lib, id] {
        lib->dislike(id, [player](bool, const QString& err) {
            status(player, err.isEmpty() ? QStringLiteral("Дизлайк поставлен") : QStringLiteral("Ошибка: ") + err);
        });
        player->next();
    });
    const QUrl web = cur ? cur->webUrl() : QUrl();
    QAction* open = menu->addAction(QStringLiteral("Открыть трек в браузере"), menu, [web] { QDesktopServices::openUrl(web); });
    for (QAction* a : {like, dislike, open}) a->setEnabled(cur != nullptr);
}

}  // namespace qiyaa
