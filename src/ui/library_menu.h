// The "Yandex Music" part of the context menus: sources (wave, likes,
// playlists, artists, albums, stations, search) and current-track actions.
#pragma once

#include <QString>
#include <QStringList>

#include <functional>

class QMenu;
class QWidget;

namespace Core {
class Player;
}  // namespace Core

namespace Ui {

// Loads a source into the player and starts playing. Status goes to Player::statusMessage.
void PlayMyWave(Core::Player* player);
void PlayWave(Core::Player* player, const QStringList& seeds, const QString& title);
void PlayLikes(Core::Player* player, bool autoplay);
void PlaySearchResults(Core::Player* player, const QString& text);

// Adds the Yandex items to `menu`. `loginRequested` opens the login dialog.
void AddLibraryActions(
    QMenu* menu,
    Core::Player* player,
    QWidget* dialogParent,
    std::function<void()> loginRequested
);

}  // namespace Ui
