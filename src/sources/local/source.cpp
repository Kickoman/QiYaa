#include "sources/local/source.h"

#include "audio/file_byte_source.h"
#include "sources/local/tracks.h"

#include <QFileInfo>
#include <QTimer>

#include <filesystem>
#include <memory>

namespace LocalSource {

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

}  // namespace LocalSource
