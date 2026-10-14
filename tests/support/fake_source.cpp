#include "support/fake_source.h"

#include <QTimer>

#include <memory>
#include <utility>

namespace Tests {

namespace {
QString EventName(Core::TrackEvent event) {
    switch (event) {
        case Core::TrackEvent::Started: return QStringLiteral("started");
        case Core::TrackEvent::Finished: return QStringLiteral("finished");
        case Core::TrackEvent::Skipped: break;
    }
    return QStringLiteral("skipped");
}
}  // namespace

FakeDownload::FakeDownload(FakeSource* owner, QString trackId)
    : source(owner)
    , id(std::move(trackId)) { }

void FakeDownload::start() {
    if (source) {
        source->startedIds << id;
    }
}

void FakeDownload::feed(const QByteArray& bytes) {
    Q_EMIT received(bytes);
}

void FakeDownload::finish(const std::optional<Core::TrackFailure>& failure) {
    Q_EMIT finished(failure);
}

QUrl FakeSource::coverUrl(const Core::Track&, int) const {
    return {};
}

QUrl FakeSource::webPage(const Core::Track&) const {
    return {};
}

void FakeSource::open(const Core::Track& track, TOpened done) {
    askedIds << track.key.id;
    answers << std::move(done);
    if (answersAtOnce) {
        const qsizetype request = answers.size() - 1;
        QTimer::singleShot(0, this, [this, request] {
            if (answers[request]) {
                complete(request);
            }
        });
    }
}

void FakeSource::trackEvent(Core::TrackEvent event, const Core::Track& track, double) {
    trackEvents << Event{event, track.key.id};
}

qsizetype FakeSource::pending() const {
    qsizetype count = 0;
    for (const TOpened& answer : answers) {
        count += answer ? 1 : 0;
    }
    return count;
}

QStringList FakeSource::eventLog() const {
    QStringList log;
    for (const Event& event : trackEvents) {
        log << EventName(event.event) + u':' + event.id;
    }
    return log;
}

QPointer<FakeDownload> FakeSource::complete(qsizetype request, int bitrateKbps) {
    const TOpened done = std::exchange(answers[request], {});
    const QString id = askedIds[request];
    auto download = std::make_unique<FakeDownload>(this, id);
    const QPointer<FakeDownload> handle(download.get());
    done(Core::OpenedTrack{std::move(download), LinkOf(id), bitrateKbps, QStringLiteral("mp3")});
    return handle;
}

void FakeSource::fail(qsizetype request, Core::FailureKind kind, const QString& text) {
    const TOpened done = std::exchange(answers[request], {});
    done(Core::TrackFailure{kind, text});
}

void FakeSource::changeFiles() {
    Q_EMIT filesChanged();
}

QUrl FakeSource::LinkOf(const QString& id) {
    return QUrl(QStringLiteral("https://fake.example/%1").arg(id));
}

}  // namespace Tests
