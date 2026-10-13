#pragma once

#include <functional>

class QMenu;
class QWidget;

namespace Core {
class Player;
class SourceRegistry;
struct Track;
}  // namespace Core

namespace YandexSource {
class Catalog;
}  // namespace YandexSource

namespace Ui {

void AddLibraryActions(
    QMenu* menu,
    Core::Player* player,
    YandexSource::Catalog* catalog,
    const Core::SourceRegistry* sources,
    const Core::Track* track,
    QWidget* dialogParent,
    std::function<void()> loginRequested
);

}  // namespace Ui
