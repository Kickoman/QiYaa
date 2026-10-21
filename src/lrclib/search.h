#pragma once

#include "core/lyrics.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

#include <optional>

namespace Lrclib {

inline constexpr qint64 kDurationToleranceMs = 2000;
inline constexpr qsizetype kMaxReplyBytes = 2 * 1024 * 1024;

struct Record {
    qint64 id = 0;
    double durationSeconds = 0;
    bool instrumental = false;
    QString plainLyrics;
    QString syncedLyrics;
};

struct Choice {
    Core::LyricsAnswer answer;
    std::optional<qint64> recordId;
};

QList<Record> ParseSearch(const QByteArray& body);
Choice Pick(const QList<Record>& records, qint64 durationMs);
QString TitleWithoutVersion(const QString& title);

}  // namespace Lrclib
