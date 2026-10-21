#include "support/fake_source.h"

#include "support/fake_continuation.h"

#include <QTimer>

#include <memory>
#include <utility>

namespace Tests {

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

void FakeSource::lyrics(const Core::Track& track, Core::TLyricsDone done) {
    lyricsAskedIds << track.key.id;
    lyricsAnswers << std::move(done);
}

qsizetype FakeSource::lyricsPending() const {
    qsizetype count = 0;
    for (const Core::TLyricsDone& answer : lyricsAnswers) {
        count += answer ? 1 : 0;
    }
    return count;
}

void FakeSource::answerLyrics(qsizetype request, Core::LyricsAnswer answer) {
    const Core::TLyricsDone done = std::exchange(lyricsAnswers[request], {});
    done(std::move(answer));
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

void FakeSource::completeWithFile(qsizetype request, std::shared_ptr<Audio::ByteSource> file) {
    const TOpened done = std::exchange(answers[request], {});
    done(Core::OpenedTrack{.link = LinkOf(askedIds[request]), .file = std::move(file)});
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
