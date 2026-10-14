#pragma once

#include "core/track.h"

#include <QList>
#include <QString>

#include <functional>

namespace Core {

struct ContinuationRequest {
    QList<Track> queueEnd{};
    bool feedback = true;
};

struct ContinuationReply {
    QList<Track> tracks{};
    QString failure{};
};

class Continuation {
public:
    using TDone = std::function<void(const ContinuationReply& reply)>;

    virtual ~Continuation() = default;

    virtual void begin(bool) { }
    virtual bool more(const ContinuationRequest& request, TDone done) = 0;
    virtual void trackEvent(TrackEvent event, const Track& track, double playedSeconds) = 0;
};

}  // namespace Core
