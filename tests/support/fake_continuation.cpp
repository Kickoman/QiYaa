#include "support/fake_continuation.h"

#include <utility>

namespace Tests {

QString EventName(Core::TrackEvent event) {
    switch (event) {
        case Core::TrackEvent::Started: return QStringLiteral("started");
        case Core::TrackEvent::Finished: return QStringLiteral("finished");
        case Core::TrackEvent::Skipped: break;
    }
    return QStringLiteral("skipped");
}

void FakeContinuation::begin(bool feedback) {
    begun << feedback;
}

bool FakeContinuation::more(const Core::ContinuationRequest& request, TDone done) {
    asked << request;
    if (refusing) {
        return false;
    }
    if (atOnce) {
        done(Core::ContinuationReply{.tracks = *atOnce});
        return true;
    }
    waiting << std::move(done);
    return true;
}

void FakeContinuation::trackEvent(Core::TrackEvent event, const Core::Track& track, double) {
    events << EventName(event) + u':' + track.key.id;
}

void FakeContinuation::answer(const QList<Core::Track>& tracks, const QString& failure) {
    if (waiting.isEmpty()) {
        return;
    }
    const TDone done = waiting.takeFirst();
    done(Core::ContinuationReply{.tracks = tracks, .failure = failure});
}

}  // namespace Tests
