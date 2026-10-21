#include "sources/spotify/source.h"

#include "sources/spotify/tracks.h"
#include "spotify/client/file_download.h"
#include "spotify/client/session.h"

#include <memory>
#include <optional>
#include <utility>

namespace SpotifySource {

namespace {

Core::FailureKind KindOf(SpotifyClient::DownloadFailure::Kind kind) {
    switch (kind) {
        case SpotifyClient::DownloadFailure::Kind::Network: return Core::FailureKind::Network;
        case SpotifyClient::DownloadFailure::Kind::KeyRefused: return Core::FailureKind::Auth;
        case SpotifyClient::DownloadFailure::Kind::Track: break;
    }
    return Core::FailureKind::Track;
}

class Download : public Core::TrackDownload {
public:
    explicit Download(std::unique_ptr<SpotifyClient::FileDownload> spotifyFile)
        : file(std::move(spotifyFile)) {
        connect(file.get(), &SpotifyClient::FileDownload::received, this, &Core::TrackDownload::received);
        connect(file.get(), &SpotifyClient::FileDownload::completed, this, [this] { Q_EMIT finished(std::nullopt); });
    }

    void start() override {
        const auto failed = [this](const SpotifyClient::DownloadFailure& failure) {
            Q_EMIT finished(Core::TrackFailure{KindOf(failure.kind), failure.text});
        };
        connect(file.get(), &SpotifyClient::FileDownload::failed, this, failed);
        file->start();
    }

    SpotifyClient::FileDownload* spotifyFile() const { return file.get(); }

private:
    std::unique_ptr<SpotifyClient::FileDownload> file;
};

}  // namespace

Source::Source(
    SpotifyClient::Web* spotifyWeb,
    SpotifyClient::Session* spotifySession,
    QNetworkAccessManager* networkAccessManager,
    QObject* parent
)
    : Core::TrackSource(parent)
    , web(spotifyWeb)
    , session(spotifySession)
    , network(networkAccessManager) { }

QUrl Source::coverUrl(const Core::Track& track, int pixels) const {
    return CoverUrlOf(track, pixels);
}

QUrl Source::webPage(const Core::Track& track) const {
    return WebPageOf(track);
}

void Source::open(const Core::Track& track, TOpened done) {
    const std::optional<SpotifyProtocol::TGid> gid = GidOf(track.key);
    if (!gid) {
        return done(Core::TrackFailure{Core::FailureKind::Track, tr("not a Spotify track")});
    }
    if (!session->hasAccount()) {
        return done(Core::TrackFailure{Core::FailureKind::Auth, tr("not logged in to Spotify")});
    }
    auto download = std::make_shared<std::unique_ptr<Download>>(
        std::make_unique<Download>(std::make_unique<SpotifyClient::FileDownload>(web, session, network, *gid, kbps))
    );
    SpotifyClient::FileDownload* file = (*download)->spotifyFile();
    auto handed = std::make_shared<bool>(false);
    connect(file, &SpotifyClient::FileDownload::prepared, file, [download, handed, done, file] {
        *handed = true;
        Core::OpenedTrack opened;
        opened.bitrateKbps = file->kbps();
        opened.codec = QStringLiteral("vorbis");
        opened.download = std::move(*download);
        done(std::move(opened));
    });
    const auto failed = [download, handed, done](const SpotifyClient::DownloadFailure& failure) {
        if (*handed) {
            return;
        }
        *handed = true;
        done(Core::TrackFailure{KindOf(failure.kind), failure.text});
        if (*download) {
            download->release()->deleteLater();
        }
    };
    connect(file, &SpotifyClient::FileDownload::failed, file, failed);
    file->prepare();
}

void Source::trackEvent(Core::TrackEvent, const Core::Track&, double) { }

void Source::lyrics(const Core::Track&, Core::TLyricsDone done) {
    done(Core::LyricsAnswer{});
}

void Source::setEconomy(bool economy) {
    kbps = economy ? kEconomyKbps : kStandardKbps;
}

}  // namespace SpotifySource
