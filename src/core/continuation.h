#pragma once

#include "core/track.h"

#include <QList>

#include <functional>

namespace Core {

class Continuation {
public:
    using TDone = std::function<void(const QList<Track>& tracks)>;

    virtual ~Continuation() = default;

    virtual void more(TDone done) = 0;
    virtual void trackEvent(TrackEvent event, const Track& track, double playedSeconds) = 0;
};

}  // namespace Core
