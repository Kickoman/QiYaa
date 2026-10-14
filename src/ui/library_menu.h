#pragma once

#include <functional>

class QMenu;
class QWidget;

namespace Core {
class SourceRegistry;
struct Track;
}  // namespace Core

namespace Yandex {
class Library;
}  // namespace Yandex

namespace YandexSource {
class Catalog;
}  // namespace YandexSource

namespace Ui {

void AddLibraryActions(
    QMenu* menu,
    Yandex::Library* library,
    YandexSource::Catalog* catalog,
    const Core::SourceRegistry* sources,
    const Core::Track* track,
    QWidget* dialogParent,
    std::function<void()> loginRequested
);

}  // namespace Ui
