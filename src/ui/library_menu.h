#pragma once

#include <functional>

class QMenu;
class QWidget;

namespace Core {
class Player;
}  // namespace Core

namespace Yandex {
struct Track;
}  // namespace Yandex

namespace YandexSource {
class Catalog;
}  // namespace YandexSource

namespace Ui {

void AddLibraryActions(
    QMenu* menu,
    Core::Player* player,
    YandexSource::Catalog* catalog,
    const Yandex::Track* track,
    QWidget* dialogParent,
    std::function<void()> loginRequested
);

}  // namespace Ui
