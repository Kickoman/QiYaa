#pragma once

#include "core/track.h"
#include "core/track_source.h"

#include <QObject>
#include <QUrl>

namespace LocalSource {

class Source : public Core::TrackSource {
    Q_OBJECT
public:
    using Core::TrackSource::TrackSource;

    QUrl coverUrl(const Core::Track& track, int pixels) const override;
    QUrl webPage(const Core::Track& track) const override;
    void open(const Core::Track& track, TOpened done) override;
    void trackEvent(Core::TrackEvent event, const Core::Track& track, double playedSeconds) override;
};

}  // namespace LocalSource
