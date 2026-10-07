#include "ui/library_menu.h"

#include "core/player.h"
#include "core/sources.h"
#include "ui/input_dialogs.h"
#include "yandex/api_client.h"
#include "yandex/library.h"

#include <QAction>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QHash>
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
        QAction* placeholder =
            submenu->addAction(QCoreApplication::translate("Ui::LibraryMenu", "Loading…"));
        placeholder->setEnabled(false);
        QPointer<QMenu> guardedMenu(submenu);
        load([guardedMenu, placeholder, fill](const QList<T>& items, const QString& error) {
            if (!guardedMenu) {
                return;
            }
            guardedMenu->removeAction(placeholder);
            placeholder->deleteLater();
            if (!error.isEmpty()) {
                guardedMenu
                    ->addAction(
                        QCoreApplication::translate("Ui::LibraryMenu", "Error: %1").arg(error)
                    )
                    ->setEnabled(false);
                return;
            }
            if (items.isEmpty()) {
                guardedMenu->addAction(QCoreApplication::translate("Ui::LibraryMenu", "(empty)"))
                    ->setEnabled(false);
                return;
            }
            fill(guardedMenu, items);
        });
    });
}

QString StationTypeTitle(const QString& type) {
    // Translated on each call: the language can change while the app runs.
    static const QMap<QString, const char*> names{
        {QStringLiteral("genre"), QT_TRANSLATE_NOOP("Ui::LibraryMenu", "Genres")},
        {QStringLiteral("mood"), QT_TRANSLATE_NOOP("Ui::LibraryMenu", "Mood")},
        {QStringLiteral("activity"), QT_TRANSLATE_NOOP("Ui::LibraryMenu", "Activities")},
        {QStringLiteral("epoch"), QT_TRANSLATE_NOOP("Ui::LibraryMenu", "Eras")},
        {QStringLiteral("local"), QT_TRANSLATE_NOOP("Ui::LibraryMenu", "Local")},
        {QStringLiteral("author"), QT_TRANSLATE_NOOP("Ui::LibraryMenu", "Authors")},
        {QStringLiteral("user"), QT_TRANSLATE_NOOP("Ui::LibraryMenu", "Personal")},
        {QStringLiteral("personal"), QT_TRANSLATE_NOOP("Ui::LibraryMenu", "Personal")},
    };
    const char* name = names.value(type, nullptr);
    return name ? QCoreApplication::translate("Ui::LibraryMenu", name) : type;
}

}  // namespace

void AddLibraryActions(
    QMenu* menu,
    Core::Player* player,
    Core::Sources* sources,
    const Yandex::Track* track,
    QWidget* dialogParent,
    std::function<void()> loginRequested
) {
    Yandex::Library* library = player->library();
    if (!library->isLoggedIn()) {
        menu->addAction(
            QCoreApplication::translate("Ui::LibraryMenu", "Log in to Yandex Music…"), menu,
            loginRequested
        );
        return;
    }

    menu->addAction(QCoreApplication::translate("Ui::LibraryMenu", "My Vibe"), menu, [sources] {
        sources->playMyWave();
    });
    menu->addAction(QCoreApplication::translate("Ui::LibraryMenu", "Liked"), menu, [sources] {
        sources->playLikes(true);
    });

    QMenu* wheel = menu->addMenu(QCoreApplication::translate("Ui::LibraryMenu", "Wheel of vibes"));
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

    QMenu* forYou = menu->addMenu(QCoreApplication::translate("Ui::LibraryMenu", "For you"));
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

    QMenu* playlists = menu->addMenu(QCoreApplication::translate("Ui::LibraryMenu", "Playlists"));
    LazySubmenu<Yandex::PlaylistReference>(
        playlists, [library](auto callback) { library->userPlaylists(callback); },
        [sources](QMenu* submenu, const QList<Yandex::PlaylistReference>& items) {
            for (const Yandex::PlaylistReference& playlist : items) {
                QMenu* playlistMenu = submenu->addMenu(
                    QStringLiteral("%1 (%2)").arg(playlist.title).arg(playlist.trackCount)
                );
                playlistMenu->addAction(
                    QCoreApplication::translate("Ui::LibraryMenu", "Listen"), playlistMenu,
                    [sources, playlist] { sources->playPlaylist(playlist); }
                );
                playlistMenu->addAction(
                    QCoreApplication::translate("Ui::LibraryMenu", "Similar tracks"), playlistMenu,
                    [sources, playlist] { sources->playRecommendations(playlist); }
                );
            }
        }
    );

    QMenu* artists = menu->addMenu(QCoreApplication::translate("Ui::LibraryMenu", "Artists"));
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

    QMenu* albums = menu->addMenu(QCoreApplication::translate("Ui::LibraryMenu", "Albums"));
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

    QMenu* stations = menu->addMenu(QCoreApplication::translate("Ui::LibraryMenu", "Stations"));
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

    menu->addAction(
        QCoreApplication::translate("Ui::LibraryMenu", "Search…"), menu,
        [sources, dialogParent] {
            const QString text =
                AskText(
                    dialogParent, QCoreApplication::translate("Ui::LibraryMenu", "Search"),
                    QCoreApplication::translate("Ui::LibraryMenu", "Artist, album or track:")
                )
                    .value_or(QString())
                    .trimmed();
            if (!text.isEmpty()) {
                sources->search(text);
            }
        }
    );

    menu->addSeparator();
    const QString id = track ? track->id : QString();
    const bool liked = track && library->isLiked(id);
    QAction* like = menu->addAction(
        liked ? QCoreApplication::translate("Ui::LibraryMenu", "Remove from Liked")
              : QCoreApplication::translate("Ui::LibraryMenu", "Like"),
        menu, [sources, id, liked] { sources->setLiked(id, !liked); }
    );
    const Yandex::Track* currentTrack = player->currentTrack();
    const bool isCurrent = track && currentTrack && id == currentTrack->id;
    QAction* dislike = menu->addAction(
        isCurrent ? QCoreApplication::translate("Ui::LibraryMenu", "Dislike (skip)")
                  : QCoreApplication::translate("Ui::LibraryMenu", "Dislike"),
        menu, [sources, id] { sources->dislikeAndSkip(id); }
    );
    const QUrl webUrl = track ? track->webUrl() : QUrl();
    QAction* open = menu->addAction(
        QCoreApplication::translate("Ui::LibraryMenu", "Open the track in the browser"), menu,
        [webUrl] { QDesktopServices::openUrl(webUrl); }
    );
    for (QAction* action : {like, dislike, open}) {
        action->setEnabled(track != nullptr);
    }
}

}  // namespace Ui
