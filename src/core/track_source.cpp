#include "core/track_source.h"

namespace Core {

void SourceRegistry::add(const QString& source, TrackSource* port) {
    ports.insert(source, port);
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

}  // namespace Core
