#pragma once

#include "audio/byte_source.h"
#include "core/failure_policy.h"
#include "core/track.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <memory>
#include <optional>
#include <variant>

namespace Core {

struct TrackFailure {
    FailureKind kind = FailureKind::Track;
    QString text;
};

class TrackDownload : public QObject {
    Q_OBJECT
public:
    virtual void start() = 0;

Q_SIGNALS:
    void received(const QByteArray& bytes);
    void finished(const std::optional<Core::TrackFailure>& failure);
};

struct OpenedTrack {
    std::unique_ptr<TrackDownload> download{};
    QUrl link{};
    int bitrateKbps = 0;
    QString codec{};
    std::shared_ptr<Audio::ByteSource> file{};
};

using TOpenResult = std::variant<OpenedTrack, TrackFailure>;

class TrackSource : public QObject {
    Q_OBJECT
public:
    using TOpened = std::function<void(TOpenResult result)>;

    using QObject::QObject;

    virtual QUrl coverUrl(const Track& track, int pixels) const = 0;
    virtual QUrl webPage(const Track& track) const = 0;
    virtual void open(const Track& track, TOpened done) = 0;
    virtual void trackEvent(TrackEvent event, const Track& track, double playedSeconds) = 0;

Q_SIGNALS:
    void filesChanged();
};

class SourceRegistry : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    void add(const QString& source, TrackSource* port);
    TrackSource* find(const QString& source) const;

    QUrl coverUrl(const Track& track, int pixels) const;
    QUrl webPage(const Track& track) const;
    void open(const Track& track, TrackSource::TOpened done);
    void trackEvent(TrackEvent event, const Track& track, double playedSeconds);

Q_SIGNALS:
    void filesChanged(const QString& source);

private:
    QHash<QString, TrackSource*> ports;
};

}  // namespace Core
