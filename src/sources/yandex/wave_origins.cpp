#include "sources/yandex/wave_origins.h"

#include "sources/yandex/tracks.h"
#include "yandex/library.h"

#include <optional>

namespace YandexSource {

void WaveOrigins::add(const QString& session, const QString& station, const Yandex::WaveBatch& batch) {
    for (const Yandex::Track& track : batch.tracks) {
        originOfTrack.insert(track.id, Origin{session, station, batch.batchId});
    }
}

void WaveOrigins::send(Yandex::Library* library, Core::TrackEvent event, const Core::Track& track, double playedSeconds)
    const {
    const std::optional<Yandex::Track> yandexTrack = YandexTrackOf(track);
    if (!yandexTrack) {
        return;
    }
    const auto origin = originOfTrack.constFind(yandexTrack->id);
    if (origin == originOfTrack.constEnd()) {
        return;
    }
    library->waveFeedback(
        origin->session, origin->station, origin->batch, WaveEventOf(event), &*yandexTrack, playedSeconds
    );
}

}  // namespace YandexSource
