#include "sources/local/tracks.h"

#include <QFileInfo>

namespace LocalSource {

bool HasAudioExtension(const QString& path) {
    return kExtensions.contains(QFileInfo(path).suffix(), Qt::CaseInsensitive);
}

Core::Track TrackOf(const QString& path, const std::optional<FileTags>& tags, const QString& cover) {
    const QFileInfo file(path);
    Core::Track track;
    track.key = {kSourceName, file.absoluteFilePath()};
    track.title = tags && !tags->title.isEmpty() ? tags->title : file.completeBaseName();
    if (tags) {
        track.artists = tags->artists;
        track.album = tags->album;
        track.durationMs = tags->durationMs;
        track.year = tags->year;
        track.genre = tags->genre;
    }
    track.cover = {cover};
    return track;
}

}  // namespace LocalSource
