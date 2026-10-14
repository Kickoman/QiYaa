#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace Core {

enum class TrackEvent { Started, Finished, Skipped };

struct TrackKey {
    QString source;
    QString id;

    bool operator==(const TrackKey&) const = default;
};

struct CoverRef {
    QString value;
};

struct Track {
    TrackKey key;
    QString title;
    QStringList artists;
    QString album;
    qint64 durationMs = 0;
    int year = 0;
    QString genre;
    CoverRef cover;
    bool available = true;

    QString displayTitle() const;
};

}  // namespace Core
