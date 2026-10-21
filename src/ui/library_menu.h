#pragma once

#include <QString>

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
    QWidget* dialogParent,
    std::function<void()> loginRequested,
    std::function<void()> logoutRequested
);

void AddTrackActions(
    QMenu* menu,
    YandexSource::Catalog* catalog,
    const Core::SourceRegistry* sources,
    const Core::Track* track
);

struct SpotifyMenuState {
    bool loggedIn = false;
    QString username;
};

void AddSpotifyActions(
    QMenu* menu,
    const SpotifyMenuState& state,
    std::function<void()> loginRequested,
    std::function<void()> logoutRequested
);

}  // namespace Ui
