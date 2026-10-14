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

bool HistoryWave::more(const Core::ContinuationRequest& request, TDone done) {
    if (session.isEmpty()) {
        const QStringList seeds = HistorySeeds(request.history);
        if (seeds.isEmpty()) {
            return false;  // CONT-06
        }
        start(seeds, request.feedback, std::move(done));
        return true;
    }
    QStringList recent;
    for (const Core::Track& track : request.queueEnd) {
        if (const QString id = CatalogId(track.key); !id.isEmpty()) {
            recent << id;
        }
    }
    std::shared_ptr<HistoryWave> self = shared_from_this();
    yandexLibrary->moreWave(session, recent, [self, done](const Yandex::WaveBatch& batch, const QString& error) {
        if (!error.isEmpty()) {
            qWarning("history vibe: %s", qPrintable(error));
        }
        self->origins.add(self->session, self->station, batch);
        done(Core::ContinuationReply{.tracks = TracksOf(batch.tracks)});
    });
    return true;
}

void HistoryWave::start(const QStringList& seeds, bool feedback, TDone done) {
    std::shared_ptr<HistoryWave> self = shared_from_this();
    yandexLibrary
        ->startWave(seeds, [self, done, seeds, feedback](const Yandex::WaveBatch& batch, const QString& error) {
            if (!error.isEmpty()) {
                qWarning("history vibe: %s", qPrintable(error));
                return done(Core::ContinuationReply{.failure = error});  // CONT-07
            }
            const bool anyAvailable = std::any_of(
                batch.tracks.cbegin(), batch.tracks.cend(), [](const Yandex::Track& track) { return track.available; }
            );
            if (!anyAvailable) {
                return done({});
            }
            self->session = batch.sessionId;
            self->station = seeds.first();
            self->origins.add(self->session, self->station, batch);
            if (feedback) {
                self->yandexLibrary->waveFeedback(
                    self->session, self->station, batch.batchId, Yandex::WaveEvent::RadioStarted
                );  // TRK-03
            }
            done(Core::ContinuationReply{.tracks = TracksOf(batch.tracks)});
        });
}

void HistoryWave::trackEvent(Core::TrackEvent event, const Core::Track& track, double playedSeconds) {
    origins.send(yandexLibrary, event, track, playedSeconds);  // CONT-08
}

Core::THistoryWaveFactory HistoryWaves(Yandex::Library* library) {
    return [library] { return std::make_shared<HistoryWave>(library); };
}

}  // namespace YandexSource
