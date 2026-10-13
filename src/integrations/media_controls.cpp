#include "integrations/media_controls.h"

#include "audio/audio_engine.h"
#include "core/cover_cache.h"
#include "core/player.h"
#include "core/track_source.h"

#include <QString>

#include <utility>

namespace Integrations {

namespace {
constexpr int kCoverPixels = 400;
}  // namespace

MediaControls::MediaControls(
    Core::Player* player,
    const Core::SourceRegistry* sources,
    Core::CoverCache* covers,
    Hooks hooks,
    QObject* parent
)
    : QObject(parent)
    , corePlayer(player)
    , trackSources(sources)
    , coverCache(covers)
    , hookFunctions(std::move(hooks)) {
    connect(corePlayer, &Core::Player::currentTrackChanged, this, [this] {
        if (const auto* track = corePlayer->currentTrack(); track && coverCache) {
            coverCache->get(trackSources->coverUrl(*track, kCoverPixels));
        }
        Q_EMIT trackChanged();
    });
    connect(corePlayer->engine(), &Audio::AudioEngine::stateChanged, this, &MediaControls::statusChanged);
    connect(corePlayer, &Core::Player::modesChanged, this, &MediaControls::modesChanged);
    connect(corePlayer, &Core::Player::seeked, this, &MediaControls::seeked);
    if (coverCache) {
        connect(coverCache, &Core::CoverCache::ready, this, [this](const QUrl& url) {
            if (remoteArtUrl() == url) {
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
        return;
    }
    corePlayer->play();
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
    const auto* track = corePlayer->currentTrack();
    return track ? trackSources->coverUrl(*track, kCoverPixels) : QUrl();
}

QUrl MediaControls::webPage() const {
    const auto* track = corePlayer->currentTrack();
    return track ? trackSources->webPage(*track) : QUrl();
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
