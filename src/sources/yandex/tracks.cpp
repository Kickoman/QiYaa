#include "sources/yandex/tracks.h"

namespace YandexSource {

Core::TrackKey KeyOf(const QString& trackId, const QString& albumId) {
    return {kSourceName, albumId.isEmpty() ? trackId : trackId + u':' + albumId};
}

Core::Track TrackOf(const Yandex::Track& track) {
    Core::Track result;
    result.key = KeyOf(track.id, track.albumId);
    result.title = track.title;
    result.artists = track.artists;
    result.album = track.albumTitle;
    result.durationMs = track.durationMs;
    result.year = track.year;
    result.genre = track.genre;
    result.cover = {track.coverUri};
    result.available = track.available;
    return result;
}

QList<Core::Track> TracksOf(const QList<Yandex::Track>& tracks) {
    QList<Core::Track> result;
    result.reserve(tracks.size());
    for (const Yandex::Track& track : tracks) {
        result << TrackOf(track);
    }
    return result;
}

std::optional<Yandex::Track> YandexTrackOf(const Core::Track& track) {
    if (track.key.source != kSourceName) {
        return std::nullopt;
    }
    Yandex::Track result;
    result.id = track.key.id.section(u':', 0, 0);
    result.albumId = track.key.id.section(u':', 1);
    result.title = track.title;
    result.artists = track.artists;
    result.albumTitle = track.album;
    result.durationMs = track.durationMs;
    result.year = track.year;
    result.genre = track.genre;
    result.coverUri = track.cover.value;
    result.available = track.available;
    return result;
}

QString CatalogId(const Core::TrackKey& key) {
    return key.source == kSourceName ? key.id.section(u':', 0, 0) : QString();
}

Yandex::WaveEvent WaveEventOf(Core::TrackEvent event) {
    switch (event) {
        case Core::TrackEvent::Started: return Yandex::WaveEvent::TrackStarted;
        case Core::TrackEvent::Finished: return Yandex::WaveEvent::TrackFinished;
        case Core::TrackEvent::Skipped: break;
    }
    return Yandex::WaveEvent::Skip;
}

}  // namespace YandexSource
