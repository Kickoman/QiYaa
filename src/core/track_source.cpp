#include "core/track_source.h"

#include <QTimer>

#include <utility>

namespace Core {

void SourceRegistry::add(const QString& source, TrackSource* port) {
    ports.insert(source, port);
    connect(port, &TrackSource::filesChanged, this, [this, source] { Q_EMIT filesChanged(source); });
}

TrackSource* SourceRegistry::find(const QString& source) const {
    return ports.value(source);
}

QUrl SourceRegistry::coverUrl(const Track& track, int pixels) const {
    const TrackSource* port = find(track.key.source);
    return port ? port->coverUrl(track, pixels) : QUrl();
}

QUrl SourceRegistry::webPage(const Track& track) const {
    const TrackSource* port = find(track.key.source);
    return port ? port->webPage(track) : QUrl();
}

void SourceRegistry::open(const Track& track, TrackSource::TOpened done) {
    if (TrackSource* port = find(track.key.source)) {
        return port->open(track, std::move(done));
    }
    const QString text = QStringLiteral("no source \"%1\" to play it").arg(track.key.source);
    QTimer::singleShot(0, this, [done = std::move(done), text] { done(TrackFailure{FailureKind::Track, text}); });
}

void SourceRegistry::trackEvent(TrackEvent event, const Track& track, double playedSeconds) {
    if (TrackSource* port = find(track.key.source)) {
        port->trackEvent(event, track, playedSeconds);
    }
}

void SourceRegistry::lyrics(const Track& track, TLyricsDone done) {
    if (TrackSource* port = find(track.key.source)) {
        return port->lyrics(track, std::move(done));
    }
    QTimer::singleShot(0, this, [done = std::move(done)] { done(LyricsAnswer{}); });
}

}  // namespace Core
