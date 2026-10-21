#pragma once

#include "core/track.h"
#include "core/track_source.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QUrl>

namespace LocalSource {

class Source : public Core::TrackSource {
    Q_OBJECT
public:
    using Core::TrackSource::TrackSource;
    ~Source() override;

    QUrl coverUrl(const Core::Track& track, int pixels) const override;
    QUrl webPage(const Core::Track& track) const override;
    void open(const Core::Track& track, TOpened done) override;
    void trackEvent(Core::TrackEvent event, const Core::Track& track, double playedSeconds) override;
    void lyrics(const Core::Track& track, Core::TLyricsDone done) override;

private:
    QList<QPointer<QThread>> readers;
};

}  // namespace LocalSource
