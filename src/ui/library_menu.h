#pragma once

#include <QString>

#include <functional>

class QMenu;

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

namespace SpotifySource {
class Catalog;
}  // namespace SpotifySource

namespace Ui {

void AddLibraryActions(
    QMenu* menu,
    Yandex::Library* library,
    YandexSource::Catalog* catalog,
    std::function<void()> searchRequested,
    std::function<void()> loginRequested,
    std::function<void()> logoutRequested
);

void AddTrackActions(
    QMenu* menu,
    YandexSource::Catalog* yandex,
    SpotifySource::Catalog* spotify,
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
    SpotifySource::Catalog* catalog,
    std::function<void()> searchRequested,
    std::function<void()> loginRequested,
    std::function<void()> logoutRequested
);

}  // namespace Ui
