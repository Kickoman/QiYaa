#include "integrations/media_controls.h"

#include "core/cover_cache.h"
#include "core/player.h"

namespace Integrations {

using Audio::AudioEngine;

namespace {
constexpr int kCoverPx = 400;
}

MediaControls::MediaControls(
    Core::Player* player,
    Core::CoverCache* covers,
    Hooks hooks,
    QObject* parent
)
    : QObject(parent)
    , corePlayer(player)
    , coverCache(covers)
    , hookFunctions(std::move(hooks)) {
    connect(corePlayer, &Core::Player::currentTrackChanged, this, [this] {
        // Make sure the cover gets downloaded, so artUrl() can become a local file.
        if (const auto* t = corePlayer->currentTrack(); t && coverCache) {
            coverCache->get(t->coverUrl(kCoverPx));
        }
        Q_EMIT trackChanged();
    });
    connect(
        corePlayer->engine(), &Audio::AudioEngine::stateChanged, this, &MediaControls::statusChanged
    );
    connect(corePlayer, &Core::Player::modesChanged, this, &MediaControls::modesChanged);
    connect(corePlayer, &Core::Player::seeked, this, &MediaControls::seeked);
    if (coverCache) {
        connect(coverCache, &Core::CoverCache::ready, this, [this](const QUrl& url) {
            if (const auto* t = corePlayer->currentTrack(); t && t->coverUrl(kCoverPx) == url) {
                Q_EMIT artChanged();
            }
        });
    }
}

MediaControls::Status MediaControls::status() const {
    switch (corePlayer->engine()->state()) {
        case Audio::AudioEngine::State::Playing:
        case Audio::AudioEngine::State::Buffering: return Status::Playing;
        case Audio::AudioEngine::State::Paused: return Status::Paused;
        case Audio::AudioEngine::State::Stopped: return Status::Stopped;
    }
    return Status::Stopped;
}

void MediaControls::play() {
    if (status() == Status::Playing) {
        return;  // media "play" must not restart the track
    }
    corePlayer->play();  // resumes when paused, starts when stopped
}

void MediaControls::pause() {
    if (status() == Status::Playing) {
        corePlayer->pause();
    }
}

void MediaControls::playPause() {
    status() == Status::Playing ? pause() : play();
}

void MediaControls::stop() {
    corePlayer->stop();
}
void MediaControls::next() {
    corePlayer->next();
}
void MediaControls::previous() {
    corePlayer->previous();
}

bool MediaControls::canSeek() const {
    return corePlayer->durationSeconds() > 0 && status() != Status::Stopped;
}

bool MediaControls::seekTo(double seconds) {
    return corePlayer->durationSeconds() > 0 && corePlayer->seekTo(seconds);
}

QUrl MediaControls::remoteArtUrl() const {
    const auto* t = corePlayer->currentTrack();
    return t ? t->coverUrl(kCoverPx) : QUrl();
}

QUrl MediaControls::artUrl() const {
    const QUrl remote = remoteArtUrl();
    if (remote.isEmpty()) {
        return {};
    }
    if (coverCache) {
        const QString local = coverCache->localFile(remote);
        if (!local.isEmpty()) {
            return QUrl::fromLocalFile(local);
        }
    }
    return remote;
}

}  // namespace Integrations
