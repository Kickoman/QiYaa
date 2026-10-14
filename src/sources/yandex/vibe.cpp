#include "sources/yandex/vibe.h"

#include "sources/yandex/tracks.h"
#include "yandex/library.h"

#include <QStringList>

#include <utility>

namespace YandexSource {

Vibe::Vibe(Yandex::Library* library, const Yandex::WaveBatch& first, QString stationId)
    : yandexLibrary(library)
    , session(first.sessionId)
    , station(std::move(stationId))
    , firstBatch(first.batchId) {
    origins.add(session, station, first);
}

void Vibe::begin(bool feedback) {
    if (!feedback || began) {
        return;
    }
    began = true;
    yandexLibrary->waveFeedback(session, station, firstBatch, Yandex::WaveEvent::RadioStarted);
}

bool Vibe::more(const Core::ContinuationRequest& request, TDone done) {
    QStringList recent;
    for (const Core::Track& track : request.queueEnd) {
        if (const QString id = CatalogId(track.key); !id.isEmpty()) {
            recent << id;
        }
    }
    std::shared_ptr<Vibe> self = shared_from_this();
    yandexLibrary->moreWave(session, recent, [self, done](const Yandex::WaveBatch& batch, const QString& error) {
        if (!error.isEmpty()) {
            qWarning("wave: %s", qPrintable(error));
        }
        self->origins.add(self->session, self->station, batch);
        done(Core::ContinuationReply{.tracks = TracksOf(batch.tracks)});
    });
    return true;
}

void Vibe::trackEvent(Core::TrackEvent event, const Core::Track& track, double playedSeconds) {
    origins.send(yandexLibrary, event, track, playedSeconds);
}

}  // namespace YandexSource
