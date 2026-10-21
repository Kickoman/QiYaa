#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

namespace LocalSource {

inline constexpr int64_t kMaxPictureBytes = 16 * 1024 * 1024;
inline constexpr int64_t kMaxReadBytes = 64 * 1024 * 1024;

struct FileTags {
    QString title;
    QStringList artists;
    QString album;
    int year = 0;
    QString genre;
    int64_t durationMs = 0;
    bool playable = true;
    QByteArray picture;

    bool operator==(const FileTags&) const = default;
};

std::optional<FileTags> ReadTags(const QString& path);
QString ReadTagLyrics(const QString& path);

}  // namespace LocalSource
