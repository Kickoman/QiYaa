#include "sources/yandex/source.h"

#include "sources/yandex/tracks.h"

namespace YandexSource {

QUrl Source::coverUrl(const Core::Track& track, int pixels) const {
    const std::optional<Yandex::Track> yandexTrack = YandexTrackOf(track);
    return yandexTrack ? yandexTrack->coverUrl(pixels) : QUrl();
}

QUrl Source::webPage(const Core::Track& track) const {
    const std::optional<Yandex::Track> yandexTrack = YandexTrackOf(track);
    return yandexTrack ? yandexTrack->webUrl() : QUrl();
}

}  // namespace YandexSource
