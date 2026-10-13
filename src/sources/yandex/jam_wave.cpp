#include "sources/yandex/jam_wave.h"

#include "sources/yandex/tracks.h"
#include "yandex/library.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace YandexSource {

using Yandex::Track;

JamWave::JamWave(Yandex::Library* library, bool feedback, TRecent recent)
    : yandexLibrary(library)
    , sendsFeedback(feedback)
    , recentTracks(std::move(recent)) { }

bool JamWave::start(const QStringList& seeds, int seedsVersion, const Core::Track* current, TDone done) {
    QStringList startSeeds = seeds;
    if (startSeeds.isEmpty() && current && !CatalogId(current->key).isEmpty()) {
        startSeeds = {QStringLiteral("track:") + CatalogId(current->key)};
    }
    if (startSeeds.isEmpty()) {
        return false;
    }
    std::shared_ptr<JamWave> self = shared_from_this();
    yandexLibrary->startWave(
        startSeeds,
        [self, done, startSeeds, seedsVersion](const Yandex::WaveBatch& batch, const QString& error) {
            const bool anyAvailable = std::any_of(batch.tracks.cbegin(), batch.tracks.cend(), [](const Track& track) {
                return track.available;
            });
            if (!error.isEmpty() || !anyAvailable) {
                if (!error.isEmpty()) {
                    qWarning("jam wave: %s", qPrintable(error));
                }
                return done({});
            }
            self->session = batch.sessionId;
            self->station = startSeeds.value(0);
            self->sessionSeedsVersion = seedsVersion;
            self->addBatch(batch);
            if (self->sendsFeedback) {
                self->yandexLibrary->waveFeedback(
                    self->session, self->station, batch.batchId, Yandex::WaveEvent::RadioStarted
                );
            }
            done(TracksOf(batch.tracks));
        }
    );
    return true;
}

void JamWave::more(TDone done) {
    QStringList recent;
    for (const Core::Track& track : recentTracks()) {
        recent << CatalogId(track.key);
    }
    std::shared_ptr<JamWave> self = shared_from_this();
    yandexLibrary->moreWave(session, recent, [self, done](const Yandex::WaveBatch& batch, const QString& error) {
        if (!error.isEmpty()) {
            qWarning("jam wave: %s", qPrintable(error));
        }
        self->addBatch(batch);
        done(TracksOf(batch.tracks));
    });
}

void JamWave::trackEvent(Core::Player::TrackEvent event, const Core::Track& track, double playedSeconds) {
    const std::optional<Track> yandexTrack = YandexTrackOf(track);
    if (!sendsFeedback || !yandexTrack) {
        return;
    }
    const auto origin = originOfTrack.constFind(yandexTrack->id);
    if (origin == originOfTrack.constEnd()) {
        return;
    }
    yandexLibrary->waveFeedback(
        origin->session, origin->station, origin->batch, WaveEventOf(event), &*yandexTrack, playedSeconds
    );
}

void JamWave::addBatch(const Yandex::WaveBatch& batch) {
    for (const Track& track : batch.tracks) {
        originOfTrack.insert(track.id, Origin{session, station, batch.batchId});
    }
}

Core::TJamWaveFactory JamWaves(Yandex::Library* library) {
    return [library](bool feedback, Core::JamWave::TRecent recent) {
        return std::make_shared<JamWave>(library, feedback, std::move(recent));
    };
}

}  // namespace YandexSource
