#pragma once

#include "core/track.h"
#include "sources/local/tags.h"

#include <QString>
#include <QStringList>

#include <optional>

namespace LocalSource {

inline const QString kSourceName = QStringLiteral("local");
inline const QStringList kExtensions = {
    QStringLiteral("mp3"),
    QStringLiteral("flac"),
    QStringLiteral("wav"),
    QStringLiteral("m4a"),
    QStringLiteral("mp4"),
    QStringLiteral("ogg"),
    QStringLiteral("oga")
};

bool HasAudioExtension(const QString& path);
Core::Track TrackOf(const QString& path, const std::optional<FileTags>& tags, const QString& cover);

}  // namespace LocalSource
