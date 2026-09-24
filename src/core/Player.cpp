#include "core/Player.h"

#include <algorithm>
#include <functional>
#include <utility>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QUuid>

namespace qiyaa {

using audio::AudioEngine;
using yandex::Track;

namespace {
constexpr int kLoadMoreWhenLeft = 2;  // endless sources: fetch more when this many tracks remain
}

Player::Player(yandex::Library* library, AudioEngine* engine, QObject* parent)
    : QObject(parent), m_library(library), m_engine(engine) {
    connect(m_engine, &AudioEngine::trackFinished, this, [this] {
        // A broken download also drains to the end; that's not "listened to the end".
        if (m_openTrack && m_openTrackEvents)
            m_openTrackEvents(m_downloadFailed ? TrackEvent::Skipped : TrackEvent::Finished, *m_openTrack, playedSeconds());
        m_openTrack.reset();
        if (m_repeat && m_playlist.size() == 1) return playIndex(m_index);
        next();
    });
    connect(m_engine, &AudioEngine::trackAdvanced, this, [this] {
        // The engine went on into the preloaded track without a gap.
        if (m_openTrack && m_openTrackEvents)
            m_openTrackEvents(m_downloadFailed ? TrackEvent::Skipped : TrackEvent::Finished, *m_openTrack, playedSeconds());
        m_openTrack.reset();
        if (!m_preload || m_preload->stream != m_engine->currentStream() || m_preload->index >= m_playlist.size()) {
            // Not ours (can't happen: a cancelled preload never plays). Resync.
            const int i = sequentialNext();
            i >= 0 ? playIndex(i) : stop();
            return;
        }
        const Preload p = *std::exchange(m_preload, std::nullopt);
        ++m_generation;
        m_download = p.reply;
        m_stream = p.stream;
        m_index = p.index;
        m_currentDownloaded = p.downloadDone && !p.failed;
        m_downloadFailed = p.failed;
        m_waitingForMore = false;
        Q_EMIT currentTrackChanged();
        maybeLoadMore();
        trackStarted(m_playlist[m_index], p.bitrate);
    });
    connect(m_engine, &AudioEngine::errorOccurred, this, [this](const QString& msg) {
        Q_EMIT statusMessage(QStringLiteral("Audio error: ") + msg);
    });
    // The engine reports end of track etc. only when polled. Poll here, not in a
    // window, so playback continues while the windows are minimised.
    m_pollTimer.setInterval(100);
    connect(&m_pollTimer, &QTimer::timeout, this, [this] {
        m_engine->poll();
        playedSeconds();  // accumulate while it plays
        Q_EMIT positionTick();
    });
    connect(m_engine, &AudioEngine::stateChanged, this, [this](AudioEngine::State s) {
        if (s == AudioEngine::State::Stopped) m_pollTimer.stop();
        else if (!m_pollTimer.isActive()) m_pollTimer.start();
    });
}

void Player::setQueue(const QList<Track>& tracks, const QString& title, bool autoplay, MoreFn more, EventFn events) {
    stop();
    m_events = std::move(events);
    m_playlist.clear();
    for (const Track& t : tracks)
        if (t.available) m_playlist << t;
    m_title = title;
    m_more = std::move(more);
    m_loadingMore = false;
    m_waitingForMore = false;
    ++m_queueGeneration;
    m_index = m_playlist.isEmpty() ? -1 : 0;
    Q_EMIT queueReplaced();
    Q_EMIT playlistChanged();
    Q_EMIT currentTrackChanged();
    if (autoplay && m_index >= 0) playIndex(0);
}

void Player::appendTracks(const QList<Track>& tracks) {
    bool added = false;
    for (const Track& t : tracks)
        if (t.available) {
            m_playlist << t;
            added = true;
        }
    if (!added) return;
    if (m_index < 0) m_index = 0;
    Q_EMIT playlistChanged();
    refreshPreload();  // e.g. more wave tracks: now there is a next one
}

void Player::removeTracks(QList<int> indices) {
    std::sort(indices.begin(), indices.end(), std::greater<>());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    bool removedCurrent = false;
    for (int i : indices) {
        if (i < 0 || i >= m_playlist.size()) continue;
        m_playlist.removeAt(i);
        if (i == m_index) removedCurrent = true;
        else if (i < m_index) --m_index;
    }
    if (removedCurrent) {
        stop();
        m_index = std::min<int>(m_index, int(m_playlist.size()) - 1);
        Q_EMIT currentTrackChanged();
    }
    if (m_playlist.isEmpty()) m_index = -1;
    Q_EMIT playlistChanged();
    refreshPreload();
}

void Player::clearQueue() {
    newSourceRequest();  // a load still in flight must not refill the cleared list
    setQueue({}, {}, false);
}

const Track* Player::currentTrack() const {
    return (m_index >= 0 && m_index < m_playlist.size()) ? &m_playlist[m_index] : nullptr;
}

double Player::durationSeconds() const {
    const auto* t = currentTrack();
    return t ? double(t->durationMs) / 1000.0 : 0.0;
}

void Player::play() {
    switch (m_engine->state()) {
    case AudioEngine::State::Paused: m_engine->resume(); return;
    case AudioEngine::State::Playing: playIndex(m_index); return;  // Winamp: Play restarts the track
    case AudioEngine::State::Buffering: return;
    case AudioEngine::State::Stopped: playIndex(m_index < 0 ? 0 : m_index); return;
    }
}

void Player::pause() {
    if (m_engine->state() == AudioEngine::State::Paused) m_engine->resume();
    else m_engine->pause();
}

void Player::closeOpenTrack() {
    if (m_openTrack && m_openTrackEvents) m_openTrackEvents(TrackEvent::Skipped, *m_openTrack, playedSeconds());
    m_openTrack.reset();
}

double Player::playedSeconds() {
    const double pos = m_engine->positionSeconds();
    const double step = pos - m_lastPosition;
    // Normal progress between two polls is ~0.1 s; bigger jumps are seeks.
    if (m_engine->state() == AudioEngine::State::Playing && step > 0 && step < 1.0) m_played += step;
    m_lastPosition = pos;
    return m_played;
}

void Player::setShuffle(bool on) {
    if (on == m_shuffle) return;
    m_shuffle = on;
    Q_EMIT modesChanged();
    refreshPreload();
}

void Player::setRepeat(bool on) {
    if (on == m_repeat) return;
    m_repeat = on;
    Q_EMIT modesChanged();
    refreshPreload();
}

void Player::stop() {
    closeOpenTrack();
    m_waitingForMore = false;
    ++m_generation;
    cancelPreload();
    abortDownload();
    m_engine->stop();
    m_stream = 0;
}

int Player::sequentialNext() const {
    if (m_playlist.isEmpty()) return -1;
    const int i = m_index + 1;
    if (i < m_playlist.size()) return i;
    if (m_more || !m_repeat) return -1;
    return 0;
}

int Player::pickNext() const {
    if (!m_shuffle || m_playlist.size() < 2) return sequentialNext();
    int i;
    do i = int(QRandomGenerator::global()->bounded(m_playlist.size()));
    while (i == m_index);
    return i;
}

void Player::next() {
    if (m_playlist.isEmpty()) return;
    // Shuffle picked the preloaded one already; in order it's the next anyway.
    if (m_preload) return playIndex(m_preload->index);
    const int i = pickNext();
    if (i >= 0) return playIndex(i);
    if (m_more) {  // endless source still loading: wait for it
        stop();
        m_waitingForMore = true;
        maybeLoadMore();
        Q_EMIT statusMessage(QStringLiteral("Загружаю ещё треки..."));
        return;
    }
    stop();
}

void Player::previous() {
    if (m_playlist.isEmpty()) return;
    playIndex(m_index > 0 ? m_index - 1 : (m_repeat ? int(m_playlist.size()) - 1 : 0));
}

bool Player::seekFraction(double fraction) {
    const double dur = durationSeconds();
    return dur > 0 && seekTo(std::clamp(fraction, 0.0, 1.0) * dur);
}

bool Player::seekTo(double seconds) {
    const double dur = durationSeconds();
    const double target = dur > 0 ? std::clamp(seconds, 0.0, dur) : std::max(0.0, seconds);
    if (!m_engine->seek(target)) return false;
    m_lastPosition = target;
    Q_EMIT seeked(target);
    return true;
}

void Player::maybeLoadMore() {
    if (!m_more || m_loadingMore || m_playlist.size() - m_index > kLoadMoreWhenLeft) return;
    m_loadingMore = true;
    const quint64 gen = m_queueGeneration;
    QPointer<Player> self(this);
    m_more([self, gen](const QList<Track>& tracks) {
        if (!self || gen != self->m_queueGeneration) return;
        self->m_loadingMore = false;
        const bool wasWaiting = self->m_waitingForMore;
        self->m_waitingForMore = false;
        self->appendTracks(tracks);
        if (wasWaiting && self->m_index + 1 < self->m_playlist.size()) self->playIndex(self->m_index + 1);
    });
}

void Player::shutDown() {
    newSourceRequest();  // loads in flight are stale now
    stop();
    m_shutDown = true;
}

void Player::playIndex(int index) {
    if (m_shutDown || index < 0 || index >= m_playlist.size()) return;
    m_waitingForMore = false;
    closeOpenTrack();
    const quint64 gen = ++m_generation;
    abortDownload();
    m_index = index;
    m_bitrate = 0;
    m_currentDownloaded = false;
    m_downloadFailed = false;
    const Track track = m_playlist[index];

    // Already downloading in the background (e.g. "next" pressed): start it from there.
    if (m_preload && m_preload->stream && m_preload->trackId == track.id && m_preload->stream == m_engine->queuedStream()) {
        const Preload p = *std::exchange(m_preload, std::nullopt);
        m_stream = m_engine->playQueuedNow();
        m_download = p.reply;
        m_currentDownloaded = p.downloadDone && !p.failed;
        m_downloadFailed = p.failed;
        Q_EMIT currentTrackChanged();
        maybeLoadMore();
        trackStarted(track, p.bitrate);
        return;
    }
    cancelPreload();
    m_stream = m_engine->beginStream();
    Q_EMIT currentTrackChanged();
    maybeLoadMore();

    m_library->api()->resolveTrackUrl(track.id, [this, gen, track](const yandex::ResolvedUrl& url, const QString& err) {
        if (gen != m_generation) return;
        if (!err.isEmpty()) {
            Q_EMIT statusMessage(QStringLiteral("Cannot get link: ") + err);
            m_engine->stop();
            return;
        }
        m_download = startDownload(url.url, m_stream);
        trackStarted(track, url.bitrateKbps);
    });
}

void Player::trackStarted(const Track& track, int bitrate) {
    m_bitrate = bitrate;
    m_library->api()->reportPlayStarted(m_library->account(), track, QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_openTrack = track;
    m_openTrackEvents = m_events;
    m_played = 0;
    m_lastPosition = 0;
    if (m_events) m_events(TrackEvent::Started, track, 0);
    Q_EMIT currentTrackChanged();
    maybePreload();
}

QNetworkReply* Player::startDownload(const QUrl& url, StreamId stream) {
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    QNetworkReply* reply = m_library->api()->network()->get(req);
    // The engine ignores data for streams it has dropped meanwhile.
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, stream] { m_engine->appendData(stream, reply->readAll()); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, stream] {
        reply->deleteLater();
        const bool failed = reply->error() != QNetworkReply::NoError;
        if (failed) {
            m_engine->failData(stream);
        } else {
            m_engine->appendData(stream, reply->readAll());
            m_engine->finishData(stream);
        }
        downloadFinished(stream, failed, reply->errorString());
    });
    return reply;
}

void Player::downloadFinished(StreamId stream, bool failed, const QString& error) {
    if (stream && stream == m_stream) {
        if (failed) {
            Q_EMIT statusMessage(QStringLiteral("Download failed: ") + error);
            m_downloadFailed = true;
            return;
        }
        m_currentDownloaded = true;
        maybePreload();  // one download at a time: now the next track
    } else if (m_preload && m_preload->stream == stream) {
        m_preload->downloadDone = true;
        m_preload->failed = failed;
    }
}

void Player::abortDownload() {
    if (QNetworkReply* r = m_download) {
        m_download.clear();
        disconnect(r, nullptr, this, nullptr);
        r->abort();
        r->deleteLater();
    }
}

void Player::maybePreload() {
    if (m_preload || m_shutDown || !m_currentDownloaded || !m_openTrack || m_engine->state() == AudioEngine::State::Stopped)
        return;
    const int index = pickNext();
    if (index < 0) return;
    Preload p;
    p.index = index;
    p.trackId = m_playlist[index].id;
    p.gen = ++m_preloadGen;
    m_preload = p;
    QPointer<Player> self(this);
    m_library->api()->resolveTrackUrl(p.trackId, [self, gen = p.gen](const yandex::ResolvedUrl& url, const QString& err) {
        if (!self || !self->m_preload || self->m_preload->gen != gen) return;
        const StreamId stream = err.isEmpty() ? self->m_engine->queueStream() : 0;
        if (!stream) {  // no link (or nothing plays any more): the track starts the usual way when it's time
            self->m_preload.reset();
            return;
        }
        self->m_preload->stream = stream;
        self->m_preload->bitrate = url.bitrateKbps;
        self->m_preload->reply = self->startDownload(url.url, stream);
    });
}

void Player::cancelPreload() {
    if (!m_preload) return;
    const Preload p = *std::exchange(m_preload, std::nullopt);
    if (QNetworkReply* r = p.reply) {
        disconnect(r, nullptr, this, nullptr);
        r->abort();
        r->deleteLater();
    }
    if (p.stream && p.stream == m_engine->queuedStream()) m_engine->clearQueued();
}

void Player::refreshPreload() {
    if (m_preload) {
        int index = -1;
        if (m_shuffle) {  // any position is fine, as long as the track is still there
            for (int i = 0; i < m_playlist.size() && index < 0; ++i)
                if (i != m_index && m_playlist[i].id == m_preload->trackId) index = i;
        } else if (const int i = sequentialNext(); i >= 0 && m_playlist[i].id == m_preload->trackId) {
            index = i;
        }
        if (index >= 0) {
            m_preload->index = index;
            return;
        }
        cancelPreload();
    }
    maybePreload();
}

}  // namespace qiyaa
