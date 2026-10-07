#pragma once

#include <functional>

class QMenu;
class QWidget;

namespace Core {
class Player;
class Sources;
}  // namespace Core

namespace Yandex {
struct Track;
}  // namespace Yandex

namespace Ui {

void AddLibraryActions(
    QMenu* menu,
    Core::Player* player,
    Core::Sources* sources,
    const Yandex::Track* track,
    QWidget* dialogParent,
    std::function<void()> loginRequested
);

}  // namespace Ui
