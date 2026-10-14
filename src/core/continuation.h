#pragma once

#include "core/track.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

namespace Core {

struct SeedHint {
    QStringList seeds{};
    int version = 0;
};

struct ContinuationRequest {
    QList<Track> queueEnd{};
    QList<Track> history{};
    bool feedback = true;
};

struct ContinuationReply {
    QList<Track> tracks{};
    QString failure{};
    std::optional<int> seedsVersion{};
};

class Continuation {
public:
    using TDone = std::function<void(const ContinuationReply& reply)>;

    virtual ~Continuation() = default;

    virtual void begin(bool) { }
    virtual bool more(const ContinuationRequest& request, TDone done) = 0;
    virtual void trackEvent(TrackEvent event, const Track& track, double playedSeconds) = 0;
};

using THistoryWaveFactory = std::function<std::shared_ptr<Continuation>()>;

}  // namespace Core
