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

void PlayMyWave(Core::Player* player);
void PlayWave(Core::Player* player, const QStringList& seeds, const QString& title);
void PlayLikes(Core::Player* player, bool autoplay);
void PlaySearchResults(Core::Player* player, const QString& text);

void AddLibraryActions(
    QMenu* menu,
    Core::Player* player,
    QWidget* dialogParent,
    std::function<void()> loginRequested
);

}  // namespace Ui
