#include "sources/yandex/tracks.h"

namespace YandexSource {

Yandex::WaveEvent WaveEventOf(Core::Player::TrackEvent event) {
    switch (event) {
        case Core::Player::TrackEvent::Started: return Yandex::WaveEvent::TrackStarted;
        case Core::Player::TrackEvent::Finished: return Yandex::WaveEvent::TrackFinished;
        case Core::Player::TrackEvent::Skipped: break;
    }
    return Yandex::WaveEvent::Skip;
}

}  // namespace YandexSource
