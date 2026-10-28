#include "ui/library_menu.h"

#include "core/track.h"
#include "core/track_source.h"
#include "sources/spotify/catalog.h"
#include "sources/spotify/library.h"
#include "sources/yandex/catalog.h"
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
using Yandex::WaveBatch;

namespace {

template <typename T>
using TListLoaded = std::function<void(const QList<T>& items, const QString& error)>;

template <typename T>
void LazySubmenu(
    QMenu* submenu,
    std::function<void(TListLoaded<T>)> load,
    std::function<void(QMenu*, const QList<T>&)> fill
) {
    auto loaded = std::make_shared<bool>(false);
    QObject::connect(submenu, &QMenu::aboutToShow, submenu, [submenu, loaded, load, fill] {
        if (*loaded) {
            return;
        }
        *loaded = true;
        QAction* placeholder = submenu->addAction(QCoreApplication::translate("Ui::LibraryMenu", "Loading…"));
        placeholder->setEnabled(false);
        QPointer<QMenu> guardedMenu(submenu);
        load([guardedMenu, placeholder, fill](const QList<T>& items, const QString& error) {
            if (!guardedMenu) {
                return;
            }
            guardedMenu->removeAction(placeholder);
            placeholder->deleteLater();
            if (!error.isEmpty()) {
                const QString text = QCoreApplication::translate("Ui::LibraryMenu", "Error: %1").arg(error);
                guardedMenu->addAction(text)->setEnabled(false);
                return;
            }
            if (items.isEmpty()) {
                guardedMenu->addAction(QCoreApplication::translate("Ui::LibraryMenu", "(empty)"))->setEnabled(false);
                return;
            }
            fill(guardedMenu, items);
        });
    });
}

void AddSpotifyList(
    QMenu* menu,
    const QString& title,
    std::function<void(TListLoaded<SpotifySource::Item>)> load,
    std::function<void(const SpotifySource::Item&)> play
) {
    LazySubmenu<SpotifySource::Item>(
        menu->addMenu(title),
        std::move(load),
        [play](QMenu* submenu, const QList<SpotifySource::Item>& items) {
            for (const SpotifySource::Item& item : items) {
                submenu->addAction(item.name, submenu, [play, item] { play(item); });
            }
        }
    );
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
    Yandex::Library* library,
    YandexSource::Catalog* catalog,
    std::function<void()> searchRequested,
    std::function<void()> loginRequested,
    std::function<void()> logoutRequested
) {
    if (!library->isLoggedIn()) {
        menu->addAction(
            QCoreApplication::translate("Ui::LibraryMenu", "Log in to Yandex Music…"), menu, loginRequested
        );
        return;
    }

    menu->addAction(QCoreApplication::translate("Ui::LibraryMenu", "My Vibe"), menu, [catalog] {
        catalog->playMyWave();
    });
    menu->addAction(QCoreApplication::translate("Ui::LibraryMenu", "Liked"), menu, [catalog] {
        catalog->playLikes(true);
    });

    QMenu* wheel = menu->addMenu(QCoreApplication::translate("Ui::LibraryMenu", "Wheel of vibes"));
    LazySubmenu<Yandex::Wave>(
        wheel,
        [library, catalog](auto callback) { library->wheelWaves(catalog->lastWaveSeeds(), callback); },
        [catalog](QMenu* submenu, const QList<Yandex::Wave>& waves) {
            for (const Yandex::Wave& wave : waves) {
                QAction* action = submenu->addAction(wave.name, submenu, [catalog, wave] {
                    catalog->playWave(wave.seeds, wave.name);
                });
                action->setToolTip(wave.description);
            }
            submenu->setToolTipsVisible(true);
        }
    );

    QMenu* forYou = menu->addMenu(QCoreApplication::translate("Ui::LibraryMenu", "For you"));
    LazySubmenu<Yandex::PlaylistReference>(
        forYou,
        [library](auto callback) { library->personalPlaylists(callback); },
        [catalog](QMenu* submenu, const QList<Yandex::PlaylistReference>& items) {
            for (const Yandex::PlaylistReference& playlist : items) {
                submenu->addAction(playlist.title, submenu, [catalog, playlist] { catalog->playPlaylist(playlist); });
            }
        }
    );

    QMenu* playlists = menu->addMenu(QCoreApplication::translate("Ui::LibraryMenu", "Playlists"));
    LazySubmenu<Yandex::PlaylistReference>(
        playlists,
        [library](auto callback) { library->userPlaylists(callback); },
        [catalog](QMenu* submenu, const QList<Yandex::PlaylistReference>& items) {
            for (const Yandex::PlaylistReference& playlist : items) {
                QMenu* playlistMenu =
                    submenu->addMenu(QStringLiteral("%1 (%2)").arg(playlist.title).arg(playlist.trackCount));
                playlistMenu->addAction(
                    QCoreApplication::translate("Ui::LibraryMenu", "Listen"),
                    playlistMenu,
                    [catalog, playlist] { catalog->playPlaylist(playlist); }
                );
                playlistMenu->addAction(
                    QCoreApplication::translate("Ui::LibraryMenu", "Similar tracks"),
                    playlistMenu,
                    [catalog, playlist] { catalog->playRecommendations(playlist); }
                );
            }
        }
    );

    QMenu* artists = menu->addMenu(QCoreApplication::translate("Ui::LibraryMenu", "Artists"));
    LazySubmenu<Yandex::NamedReference>(
        artists,
        [library](auto callback) { library->likedArtists(callback); },
        [catalog](QMenu* submenu, const QList<Yandex::NamedReference>& items) {
            for (const Yandex::NamedReference& artist : items) {
                submenu->addAction(artist.name, submenu, [catalog, artist] {
                    catalog->playArtist(artist.id, artist.name);
                });
            }
        }
    );

    QMenu* albums = menu->addMenu(QCoreApplication::translate("Ui::LibraryMenu", "Albums"));
    LazySubmenu<Yandex::NamedReference>(
        albums,
        [library](auto callback) { library->likedAlbums(callback); },
        [catalog](QMenu* submenu, const QList<Yandex::NamedReference>& items) {
            for (const Yandex::NamedReference& album : items) {
                submenu->addAction(album.name, submenu, [catalog, album] { catalog->playAlbum(album.id, album.name); });
            }
        }
    );

    QMenu* stations = menu->addMenu(QCoreApplication::translate("Ui::LibraryMenu", "Stations"));
    LazySubmenu<Yandex::Station>(
        stations,
        [library](auto callback) { library->stations(callback); },
        [catalog](QMenu* submenu, const QList<Yandex::Station>& items) {
            QMap<QString, QMenu*> groups;
            for (const Yandex::Station& station : items) {
                QMenu*& group = groups[station.type];
                if (!group) {
                    group = submenu->addMenu(StationTypeTitle(station.type));
                }
                group->addAction(station.name, group, [catalog, station] {
                    catalog->playWave({station.id}, station.name);
                });
            }
        }
    );

    menu->addAction(QCoreApplication::translate("Ui::LibraryMenu", "Search…"), menu, searchRequested);

    menu->addSeparator();
    const QString name = library->account().displayName;
    menu->addAction(QCoreApplication::translate("Ui::LibraryMenu", "Log out (%1)").arg(name), menu, logoutRequested);
}

void AddTrackActions(
    QMenu* menu,
    YandexSource::Catalog* yandex,
    SpotifySource::Catalog* spotify,
    const Core::SourceRegistry* sources,
    const Core::Track* track
) {
    const Core::TrackKey key = track ? track->key : Core::TrackKey{};
    const bool ownTrack = track && yandex->owns(key);
    const bool liked = ownTrack && yandex->isLiked(key);
    const QString likeText = liked
        ? QCoreApplication::translate("Ui::LibraryMenu", "Remove from Liked")
        : QCoreApplication::translate("Ui::LibraryMenu", "Like");
    QAction* like = menu->addAction(likeText, menu, [yandex, key, liked] { yandex->setLiked(key, !liked); });
    const bool isCurrent = ownTrack && yandex->isCurrent(key);
    const QString dislikeText = isCurrent
        ? QCoreApplication::translate("Ui::LibraryMenu", "Dislike (skip)")
        : QCoreApplication::translate("Ui::LibraryMenu", "Dislike");
    QAction* dislike = menu->addAction(dislikeText, menu, [yandex, key] { yandex->dislikeAndSkip(key); });
    const QUrl webUrl = track ? sources->webPage(*track) : QUrl();
    QAction* open = menu->addAction(
        QCoreApplication::translate("Ui::LibraryMenu", "Open the track in the browser"),
        menu,
        [webUrl] { QDesktopServices::openUrl(webUrl); }
    );
    like->setEnabled(ownTrack);
    dislike->setEnabled(ownTrack);
    open->setEnabled(!webUrl.isEmpty());
    if (track && spotify->owns(key)) {
        const Core::Track seed = *track;
        QAction* radio = menu->addAction(
            QCoreApplication::translate("Ui::LibraryMenu", "Radio"), menu, [spotify, seed] { spotify->playRadio(seed); }
        );
        menu->insertAction(open, radio);
    }
}

void AddSpotifyActions(
    QMenu* menu,
    const SpotifyMenuState& state,
    SpotifySource::Catalog* catalog,
    std::function<void()> searchRequested,
    std::function<void()> loginRequested,
    std::function<void()> logoutRequested
) {
    if (!state.loggedIn) {
        menu->addAction(QCoreApplication::translate("Ui::LibraryMenu", "Log in to Spotify…"), menu, loginRequested);
        return;
    }
    SpotifySource::Library* library = catalog->library();
    menu->addAction(QCoreApplication::translate("Ui::LibraryMenu", "Liked Songs"), menu, [catalog] {
        catalog->playLikes();
    });
    const auto playPlaylist = [catalog](const SpotifySource::Item& item) { catalog->playPlaylist(item); };
    AddSpotifyList(
        menu,
        QCoreApplication::translate("Ui::LibraryMenu", "For you"),
        [library](auto loaded) { library->forYou(loaded); },
        playPlaylist
    );
    AddSpotifyList(
        menu,
        QCoreApplication::translate("Ui::LibraryMenu", "Playlists"),
        [library](auto loaded) { library->playlists(loaded); },
        playPlaylist
    );
    AddSpotifyList(
        menu,
        QCoreApplication::translate("Ui::LibraryMenu", "Artists"),
        [library](auto loaded) { library->followedArtists(loaded); },
        [catalog](const SpotifySource::Item& item) { catalog->playArtist(item); }
    );
    AddSpotifyList(
        menu,
        QCoreApplication::translate("Ui::LibraryMenu", "Albums"),
        [library](auto loaded) { library->savedAlbums(loaded); },
        [catalog](const SpotifySource::Item& item) { catalog->playAlbum(item); }
    );
    menu->addAction(QCoreApplication::translate("Ui::LibraryMenu", "Search…"), menu, searchRequested);
    menu->addSeparator();
    menu->addAction(
        QCoreApplication::translate("Ui::LibraryMenu", "Log out (%1)").arg(state.username), menu, logoutRequested
    );
}

}  // namespace Ui
