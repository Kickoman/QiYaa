#include "sources/yandex/history_wave.h"

#include "sources/yandex/tracks.h"
#include "yandex/library.h"

#include <algorithm>
#include <utility>

namespace YandexSource {

QStringList HistorySeeds(const QList<Core::Track>& history) {
    QStringList seeds;
    for (const Core::Track& track : history) {
        const QString id = CatalogId(track.key);
        if (!id.isEmpty() && !seeds.contains(QStringLiteral("track:") + id)) {
            seeds << QStringLiteral("track:") + id;
        }
        if (seeds.size() == kHistorySeeds) {
            break;
        }
    }
    return seeds;
}

HistoryWave::HistoryWave(Yandex::Library* library)
    : yandexLibrary(library) { }

void HistoryWave::begin(bool feedback) {
    if (!feedback || session.isEmpty() || sessionSeedsVersion || radioStarted) {
        return;
    }
    radioStarted = true;
    yandexLibrary->waveFeedback(session, station, firstBatch, Yandex::WaveEvent::RadioStarted);
}

bool HistoryWave::more(const Core::ContinuationRequest& request, TDone done) {
    if (session.isEmpty() || !goesOn(request)) {
        const bool jamSeeds = request.jamSeeds && !request.jamSeeds->seeds.isEmpty();
        const QStringList seeds = jamSeeds ? request.jamSeeds->seeds : HistorySeeds(request.history);
        if (seeds.isEmpty()) {
            return false;  // CONT-06
        }
        start(seeds, jamSeeds ? std::optional(request.jamSeeds->version) : std::nullopt, request.feedback, done);
        return true;
    }
    QStringList recent;
    for (const Core::Track& track : request.queueEnd) {
        if (const QString id = CatalogId(track.key); !id.isEmpty()) {
            recent << id;
        }
    }
    std::shared_ptr<HistoryWave> self = shared_from_this();
    const int askedStart = starts;
    yandexLibrary
        ->moreWave(session, recent, [self, done, askedStart](const Yandex::WaveBatch& batch, const QString& error) {
            if (!error.isEmpty()) {
                qWarning("history vibe: %s", qPrintable(error));
            }
            if (askedStart != self->starts) {
                return done({});
            }
            self->origins.add(self->session, self->station, batch);
            done(Core::ContinuationReply{.tracks = TracksOf(batch.tracks), .seedsVersion = self->sessionSeedsVersion});
        });
    return true;
}

bool HistoryWave::goesOn(const Core::ContinuationRequest& request) const {
    if (!request.jamSeeds) {
        return !sessionSeedsVersion;  // HOST-32
    }
    return request.jamSeeds->seeds.isEmpty() || sessionSeedsVersion == request.jamSeeds->version;  // HOST-11
}

void HistoryWave::start(const QStringList& seeds, std::optional<int> seedsVersion, bool feedback, TDone done) {
    std::shared_ptr<HistoryWave> self = shared_from_this();
    const int startNumber = ++starts;
    yandexLibrary->startWave(
        seeds,
        [self, done, seeds, seedsVersion, feedback, startNumber](const Yandex::WaveBatch& batch, const QString& error) {
            if (startNumber != self->starts) {
                return done({});
            }
            if (!error.isEmpty()) {
                qWarning("history vibe: %s", qPrintable(error));
                return done(Core::ContinuationReply{.failure = error, .seedsVersion = seedsVersion});  // CONT-07
            }
            const bool anyAvailable = std::any_of(
                batch.tracks.cbegin(), batch.tracks.cend(), [](const Yandex::Track& track) { return track.available; }
            );
            if (!anyAvailable) {
                return done(Core::ContinuationReply{.seedsVersion = seedsVersion});
            }
            self->session = batch.sessionId;
            self->station = seeds.first();
            self->firstBatch = batch.batchId;
            self->sessionSeedsVersion = seedsVersion;
            self->radioStarted = feedback;
            self->origins.add(self->session, self->station, batch);
            if (feedback) {
                self->yandexLibrary->waveFeedback(
                    self->session, self->station, batch.batchId, Yandex::WaveEvent::RadioStarted
                );  // TRK-03
            }
            done(Core::ContinuationReply{.tracks = TracksOf(batch.tracks), .seedsVersion = seedsVersion});
        }
    );
}

void HistoryWave::trackEvent(Core::TrackEvent event, const Core::Track& track, double playedSeconds) {
    origins.send(yandexLibrary, event, track, playedSeconds);  // CONT-08, HOST-16
}

Core::THistoryWaveFactory HistoryWaves(Yandex::Library* library) {
    return [library] { return std::make_shared<HistoryWave>(library); };
}

}  // namespace YandexSource
