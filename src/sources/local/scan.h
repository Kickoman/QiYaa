#pragma once

#include "core/track.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>

namespace LocalSource {

inline constexpr int kMaxFiles = 10'000;
inline constexpr int kMaxFolders = 10'000;
inline constexpr int kCoverPixels = 400;

struct ScanResult {
    QList<Core::Track> tracks;
    bool cut = false;
};

ScanResult ScanFiles(const QStringList& paths, const QString& coverDirectory, const std::atomic<bool>& cancelled);
ScanResult ScanFolder(const QString& folder, const QString& coverDirectory, const std::atomic<bool>& cancelled);

}  // namespace LocalSource
