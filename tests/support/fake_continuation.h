#pragma once

#include "core/continuation.h"
#include "core/track.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace Tests {

QString EventName(Core::TrackEvent event);

class FakeContinuation : public Core::Continuation {
public:
    void begin(bool feedback) override;
    bool more(const Core::ContinuationRequest& request, TDone done) override;
    bool followsJamSeeds() const override { return seedsFollowed; }
    void trackEvent(Core::TrackEvent event, const Core::Track& track, double playedSeconds) override;

    void answerAtOnce(const QList<Core::Track>& tracks) { atOnce = tracks; }
    void refuse(bool on) { refusing = on; }
    void followSeeds(bool on) { seedsFollowed = on; }
    void answer(const QList<Core::Track>& tracks, const QString& failure = {});

    qsizetype pending() const { return waiting.size(); }
    const QList<Core::ContinuationRequest>& requests() const { return asked; }
    const QList<bool>& begins() const { return begun; }
    const QStringList& eventLog() const { return events; }

private:
    std::optional<QList<Core::Track>> atOnce;
    bool refusing = false;
    bool seedsFollowed = true;
    QList<TDone> waiting;
    QList<Core::ContinuationRequest> asked;
    QList<bool> begun;
    QStringList events;
};

}  // namespace Tests
