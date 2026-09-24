#include "integrations/MediaControls.h"

#include "core/CoverCache.h"
#include "core/Player.h"

namespace qiyaa {

using audio::AudioEngine;

namespace {
constexpr int kCoverPx = 400;
}

MediaControls::MediaControls(Player* player, CoverCache* covers, Hooks hooks, QObject* parent)
    : QObject(parent), m_player(player), m_covers(covers), m_hooks(std::move(hooks)) {
    connect(m_player, &Player::currentTrackChanged, this, [this] {
        // Make sure the cover gets downloaded, so artUrl() can become a local file.
        if (const auto* t = m_player->currentTrack(); t && m_covers) m_covers->get(t->coverUrl(kCoverPx));
        Q_EMIT trackChanged();
    });
    connect(m_player->engine(), &AudioEngine::stateChanged, this, &MediaControls::statusChanged);
    connect(m_player, &Player::modesChanged, this, &MediaControls::modesChanged);
    connect(m_player, &Player::seeked, this, &MediaControls::seeked);
    if (m_covers) {
        connect(m_covers, &CoverCache::ready, this, [this](const QUrl& url) {
            if (const auto* t = m_player->currentTrack(); t && t->coverUrl(kCoverPx) == url) Q_EMIT artChanged();
        });
    }
}

MediaControls::Status MediaControls::status() const {
    switch (m_player->engine()->state()) {
    case AudioEngine::State::Playing:
    case AudioEngine::State::Buffering: return Status::Playing;
    case AudioEngine::State::Paused: return Status::Paused;
    case AudioEngine::State::Stopped: return Status::Stopped;
    }
    return Status::Stopped;
}

void MediaControls::play() {
    if (status() == Status::Playing) return;  // media "play" must not restart the track
    m_player->play();                          // resumes when paused, starts when stopped
}

void MediaControls::pause() {
    if (status() == Status::Playing) m_player->pause();
}

void MediaControls::playPause() {
    status() == Status::Playing ? pause() : play();
}

void MediaControls::stop() { m_player->stop(); }
void MediaControls::next() { m_player->next(); }
void MediaControls::previous() { m_player->previous(); }

bool MediaControls::canSeek() const {
    return m_player->durationSeconds() > 0 && status() != Status::Stopped;
}

bool MediaControls::seekTo(double seconds) {
    return m_player->durationSeconds() > 0 && m_player->seekTo(seconds);
}

QUrl MediaControls::remoteArtUrl() const {
    const auto* t = m_player->currentTrack();
    return t ? t->coverUrl(kCoverPx) : QUrl();
}

QUrl MediaControls::artUrl() const {
    const QUrl remote = remoteArtUrl();
    if (remote.isEmpty()) return {};
    if (m_covers) {
        const QString local = m_covers->localFile(remote);
        if (!local.isEmpty()) return QUrl::fromLocalFile(local);
    }
    return remote;
}

}  // namespace qiyaa
