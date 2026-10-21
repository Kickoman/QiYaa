#pragma once

#include "core/lyrics.h"

#include <QString>

#include <optional>

namespace LocalSource {

QString LrcPathOf(const QString& audioPath);
std::optional<QString> ReadLrc(const QString& path);
Core::LyricsAnswer ReadLyrics(const QString& audioPath);

}  // namespace LocalSource
