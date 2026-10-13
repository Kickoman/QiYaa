#pragma once

#include "core/track.h"

#include <QHash>
#include <QString>
#include <QUrl>

namespace Core {

class TrackSource {
public:
    virtual ~TrackSource() = default;

    virtual QUrl coverUrl(const Track& track, int pixels) const = 0;
    virtual QUrl webPage(const Track& track) const = 0;
};

class SourceRegistry {
public:
    void add(const QString& source, TrackSource* port);
    TrackSource* find(const QString& source) const;

    QUrl coverUrl(const Track& track, int pixels) const;
    QUrl webPage(const Track& track) const;

private:
    QHash<QString, TrackSource*> ports;
};

}  // namespace Core
