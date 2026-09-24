// The "Yandex Music" part of the context menus: sources (wave, likes,
// playlists, artists, albums, stations, search) and current-track actions.
#pragma once

#include <functional>

#include <QString>
#include <QStringList>

class QMenu;
class QWidget;

namespace qiyaa {

class Player;

namespace sources {
// Loads a source into the player and starts playing. Status goes to Player::statusMessage.
void playMyWave(Player* player);
void playWave(Player* player, const QStringList& seeds, const QString& title);
void playLikes(Player* player, bool autoplay);
void search(Player* player, const QString& text);
}  // namespace sources

// Adds the Yandex items to `menu`. `loginRequested` opens the login dialog.
void addLibraryActions(QMenu* menu, Player* player, QWidget* dialogParent, std::function<void()> loginRequested);

}  // namespace qiyaa
