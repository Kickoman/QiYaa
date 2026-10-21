#include "sources/local/source.h"

#include "audio/file_byte_source.h"
#include "sources/local/lyrics.h"
#include "sources/local/tracks.h"

#include <QFileInfo>
#include <QTimer>

#include <filesystem>
#include <memory>

namespace LocalSource {

Source::~Source() {
    for (const QPointer<QThread>& reader : readers) {
        if (reader) {
            reader->wait();
        }
    }
}

QUrl Source::coverUrl(const Core::Track& track, int) const {
    if (track.key.source != kSourceName || track.cover.value.isEmpty()) {
        return {};
    }
    return QUrl::fromLocalFile(track.cover.value);
}

QUrl Source::webPage(const Core::Track&) const {
    return {};
}

void Source::open(const Core::Track& track, TOpened done) {
    QTimer::singleShot(0, this, [path = track.key.id, durationMs = track.durationMs, done = std::move(done)] {
        std::shared_ptr<Audio::FileByteSource> file =
            Audio::FileByteSource::Open(std::filesystem::path(path.toStdU16String()));
        if (!file) {
            return done(Core::TrackFailure{
                Core::FailureKind::Track, QStringLiteral("cannot open the file %1").arg(QFileInfo(path).fileName())
            });
        }
        const qint64 bits = static_cast<qint64>(file->available()) * 8;
        done(Core::OpenedTrack{
            .bitrateKbps = durationMs > 0 ? static_cast<int>(bits / durationMs) : 0,
            .codec = QFileInfo(path).suffix().toLower(),
            .file = std::move(file)
        });
    });
}

void Source::trackEvent(Core::TrackEvent, const Core::Track&, double) { }

void Source::lyrics(const Core::Track& track, Core::TLyricsDone done) {
    if (track.key.source != kSourceName) {
        QTimer::singleShot(0, this, [done = std::move(done)] { done(Core::LyricsAnswer{}); });
        return;
    }
    QThread* reader = QThread::create([this, path = track.key.id, done = std::move(done)] {
        const Core::LyricsAnswer answer = ReadLyrics(path);
        QMetaObject::invokeMethod(this, [done, answer] { done(answer); });
    });
    reader->setParent(this);
    connect(reader, &QThread::finished, reader, &QObject::deleteLater);
    readers.removeAll(nullptr);
    readers << reader;
    reader->start();
}

}  // namespace LocalSource
