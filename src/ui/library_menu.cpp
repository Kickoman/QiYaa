#include "ui/library_menu.h"

#include "core/player.h"
#include "core/sources.h"
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

void AddLibraryActions(
    QMenu* menu,
    Core::Player* player,
    Core::Sources* sources,
    QWidget* dialogParent,
    std::function<void()> loginRequested
) {
    Yandex::Library* library = player->library();
    if (!library->isLoggedIn()) {
        menu->addAction(QStringLiteral("Войти в Яндекс Музыку..."), menu, loginRequested);
        return;
    }

    menu->addAction(QStringLiteral("Моя волна"), menu, [sources] { sources->playMyWave(); });
    menu->addAction(QStringLiteral("Мне нравится"), menu, [sources] { sources->playLikes(true); });

    QMenu* wheel = menu->addMenu(QStringLiteral("Колесо волн"));
    LazySubmenu<Yandex::Wave>(
        wheel,
        [library, sources](auto callback) {
            library->wheelWaves(sources->lastWaveSeeds(), callback);
        },
        [sources](QMenu* submenu, const QList<Yandex::Wave>& waves) {
            for (const Yandex::Wave& wave : waves) {
                QAction* action = submenu->addAction(wave.name, submenu, [sources, wave] {
                    sources->playWave(wave.seeds, wave.name);
                });
                action->setToolTip(wave.description);
            }
            submenu->setToolTipsVisible(true);
        }
    );

    QMenu* forYou = menu->addMenu(QStringLiteral("Для вас"));
    LazySubmenu<Yandex::PlaylistReference>(
        forYou, [library](auto callback) { library->personalPlaylists(callback); },
        [sources](QMenu* submenu, const QList<Yandex::PlaylistReference>& items) {
            for (const Yandex::PlaylistReference& playlist : items) {
                submenu->addAction(playlist.title, submenu, [sources, playlist] {
                    sources->playPlaylist(playlist);
                });
            }
        }
    );

    QMenu* playlists = menu->addMenu(QStringLiteral("Плейлисты"));
    LazySubmenu<Yandex::PlaylistReference>(
        playlists, [library](auto callback) { library->userPlaylists(callback); },
        [sources](QMenu* submenu, const QList<Yandex::PlaylistReference>& items) {
            for (const Yandex::PlaylistReference& playlist : items) {
                QMenu* playlistMenu = submenu->addMenu(
                    QStringLiteral("%1 (%2)").arg(playlist.title).arg(playlist.trackCount)
                );
                playlistMenu->addAction(
                    QStringLiteral("Слушать"), playlistMenu,
                    [sources, playlist] { sources->playPlaylist(playlist); }
                );
                playlistMenu->addAction(
                    QStringLiteral("Похожие треки"), playlistMenu,
                    [sources, playlist] { sources->playRecommendations(playlist); }
                );
            }
        }
    );

    QMenu* artists = menu->addMenu(QStringLiteral("Исполнители"));
    LazySubmenu<Yandex::NamedReference>(
        artists, [library](auto callback) { library->likedArtists(callback); },
        [sources](QMenu* submenu, const QList<Yandex::NamedReference>& items) {
            for (const Yandex::NamedReference& artist : items) {
                submenu->addAction(artist.name, submenu, [sources, artist] {
                    sources->playArtist(artist.id, artist.name);
                });
            }
        }
    );

    QMenu* albums = menu->addMenu(QStringLiteral("Альбомы"));
    LazySubmenu<Yandex::NamedReference>(
        albums, [library](auto callback) { library->likedAlbums(callback); },
        [sources](QMenu* submenu, const QList<Yandex::NamedReference>& items) {
            for (const Yandex::NamedReference& album : items) {
                submenu->addAction(album.name, submenu, [sources, album] {
                    sources->playAlbum(album.id, album.name);
                });
            }
        }
    );

    QMenu* stations = menu->addMenu(QStringLiteral("Станции"));
    LazySubmenu<Yandex::Station>(
        stations, [library](auto callback) { library->stations(callback); },
        [sources](QMenu* submenu, const QList<Yandex::Station>& items) {
            QMap<QString, QMenu*> groups;
            for (const Yandex::Station& station : items) {
                QMenu*& group = groups[station.type];
                if (!group) {
                    group = submenu->addMenu(StationTypeTitle(station.type));
                }
                group->addAction(station.name, group, [sources, station] {
                    sources->playWave({station.id}, station.name);
                });
            }
        }
    );

    menu->addAction(QStringLiteral("Поиск..."), menu, [sources, dialogParent] {
        bool ok = false;
        const QString text = QInputDialog::getText(
            dialogParent, QStringLiteral("Поиск"), QStringLiteral("Исполнитель, альбом или трек:"),
            QLineEdit::Normal, {}, &ok
        );
        if (ok && !text.trimmed().isEmpty()) {
            sources->search(text.trimmed());
        }
    });

    menu->addSeparator();
    const Yandex::Track* currentTrack = player->currentTrack();
    const QString id = currentTrack ? currentTrack->id : QString();
    const bool liked = currentTrack && library->isLiked(id);
    QAction* like = menu->addAction(
        liked ? QStringLiteral("Убрать из «Мне нравится»") : QStringLiteral("Нравится"), menu,
        [sources, id, liked] { sources->setLiked(id, !liked); }
    );
    QAction* dislike =
        menu->addAction(QStringLiteral("Не нравится (пропустить)"), menu, [sources, id] {
            sources->dislikeAndSkip(id);
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
