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
void playMyWave(Core::Player* player);
void playWave(Core::Player* player, const QStringList& seeds, const QString& title);
void playLikes(Core::Player* player, bool autoplay);
void playSearchResults(Core::Player* player, const QString& text);

// Adds the Yandex items to `menu`. `loginRequested` opens the login dialog.
void addLibraryActions(
    QMenu* menu,
    Core::Player* player,
    QWidget* dialogParent,
    std::function<void()> loginRequested
);

}  // namespace Ui
