#pragma once

#include "audio/byte_source.h"
#include "core/failure_policy.h"
#include "core/track.h"
#include "core/track_source.h"

#include <QByteArray>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <memory>
#include <optional>

namespace Tests {

class FakeSource;

class FakeDownload : public Core::TrackDownload {
    Q_OBJECT
public:
    FakeDownload(FakeSource* owner, QString trackId);

    void start() override;
    void feed(const QByteArray& bytes);
    void finish(const std::optional<Core::TrackFailure>& failure = std::nullopt);

private:
    QPointer<FakeSource> source;
    QString id;
};

class FakeSource : public Core::TrackSource {
    Q_OBJECT
public:
    struct Event {
        Core::TrackEvent event;
        QString id;
    };

    using Core::TrackSource::TrackSource;

    QUrl coverUrl(const Core::Track& track, int pixels) const override;
    QUrl webPage(const Core::Track& track) const override;
    void open(const Core::Track& track, TOpened done) override;
    void trackEvent(Core::TrackEvent event, const Core::Track& track, double playedSeconds) override;

    qsizetype pending() const;
    const QStringList& asked() const { return askedIds; }
    const QStringList& started() const { return startedIds; }
    const QList<Event>& events() const { return trackEvents; }
    QStringList eventLog() const;

    void setAnswersAtOnce(bool on) { answersAtOnce = on; }
    QPointer<FakeDownload> complete(qsizetype request, int bitrateKbps = 320);
    void completeWithFile(qsizetype request, std::shared_ptr<Audio::ByteSource> file);
    void fail(qsizetype request, Core::FailureKind kind, const QString& text = QStringLiteral("broken"));
    void changeFiles();
    static QUrl LinkOf(const QString& id);

private:
    friend class FakeDownload;

    bool answersAtOnce = false;
    QList<TOpened> answers;
    QStringList askedIds;
    QStringList startedIds;
    QList<Event> trackEvents;
};

}  // namespace Tests
