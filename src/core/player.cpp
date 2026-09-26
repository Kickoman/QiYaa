#include "core/player.h"

#include "yandex/api_client.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QUuid>

#include <algorithm>
#include <functional>
#include <utility>

namespace Core {

using Audio::AudioEngine;
using Yandex::Track;

namespace {
constexpr int kLoadMoreWhenLeft = 2;
constexpr int kDownloadTimeoutMs = 30'000;
}  // namespace

Player::Player(Yandex::Library* library, Audio::AudioEngine* engine, QObject* parent)
    : QObject(parent)
    , yandexLibrary(library)
    , audioEngine(engine) {
    connect(audioEngine, &Audio::AudioEngine::trackFinished, this, [this] {
        if (openTrack && openTrackEvents) {
            openTrackEvents(
                downloadFailed ? TrackEvent::Skipped : TrackEvent::Finished, *openTrack,
                accumulatePlayedSeconds()
            );
        }
        openTrack.reset();
        if (repeatEnabled && queuedTracks.size() == 1) {
            return playIndex(playingIndex);
        }
        next();
    });
    connect(audioEngine, &Audio::AudioEngine::trackAdvanced, this, [this] {
        if (openTrack && openTrackEvents) {
            openTrackEvents(
                downloadFailed ? TrackEvent::Skipped : TrackEvent::Finished, *openTrack,
                accumulatePlayedSeconds()
            );
        }
        openTrack.reset();
        if (!preload || preload->stream != audioEngine->currentStream()
            || preload->index >= queuedTracks.size()) {
            const int i = sequentialNext();
            i >= 0 ? playIndex(i) : stop();
            return;
        }
        const Preload upcoming = *std::exchange(preload, std::nullopt);
        ++generation;
        download = upcoming.reply;
        streamId = upcoming.stream;
        playingIndex = upcoming.index;
        currentDownloaded = upcoming.downloadDone && !upcoming.failed;
        downloadFailed = upcoming.failed;
        waitingForMore = false;
        Q_EMIT currentTrackChanged();
        maybeLoadMore();
        trackStarted(queuedTracks[playingIndex], upcoming.bitrate);
    });
    connect(audioEngine, &Audio::AudioEngine::errorOccurred, this, [this](const QString& msg) {
        Q_EMIT statusMessage(QStringLiteral("Audio error: ") + msg);
    });
    pollTimer.setInterval(100);
    connect(&pollTimer, &QTimer::timeout, this, [this] {
        audioEngine->poll();
        accumulatePlayedSeconds();
        Q_EMIT positionTick();
    });
    connect(
        audioEngine, &Audio::AudioEngine::stateChanged, this,
        [this](Audio::AudioEngine::State state) {
            if (state == Audio::AudioEngine::State::Stopped) {
                pollTimer.stop();
            } else if (!pollTimer.isActive()) {
                pollTimer.start();
            }
        }
    );
}

void Player::setQueue(
    const QList<Yandex::Track>& tracks,
    const QString& title,
    bool autoplay,
    TMoreFn more,
    TEventFn events
) {
    stop();
    reportEvent = std::move(events);
    queuedTracks.clear();
    for (const Yandex::Track& track : tracks) {
        if (track.available) {
            queuedTracks << track;
        }
    }
    titleText = title;
    loadMore = std::move(more);
    loadingMore = false;
    waitingForMore = false;
    ++queueGeneration;
    playingIndex = queuedTracks.isEmpty() ? -1 : 0;
    Q_EMIT queueReplaced();
    Q_EMIT playlistChanged();
    Q_EMIT currentTrackChanged();
    if (autoplay && playingIndex >= 0) {
        playIndex(0);
    }
}

void Player::appendTracks(const QList<Yandex::Track>& tracks) {
    bool added = false;
    for (const Yandex::Track& track : tracks) {
        if (track.available) {
            queuedTracks << track;
            added = true;
        }
    }
    if (!added) {
        return;
    }
    if (playingIndex < 0) {
        playingIndex = 0;
    }
    Q_EMIT playlistChanged();
    refreshPreload();
}

void Player::removeTracks(QList<int> indices) {
    std::sort(indices.begin(), indices.end(), std::greater<>());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    bool removedCurrent = false;
    for (int i : indices) {
        if (i < 0 || i >= queuedTracks.size()) {
            continue;
        }
        queuedTracks.removeAt(i);
        if (i == playingIndex) {
            removedCurrent = true;
        } else if (i < playingIndex) {
            --playingIndex;
        }
    }
    if (removedCurrent) {
        stop();
        playingIndex = std::min<int>(playingIndex, int(queuedTracks.size()) - 1);
        Q_EMIT currentTrackChanged();
    }
    if (queuedTracks.isEmpty()) {
        playingIndex = -1;
    }
    Q_EMIT playlistChanged();
    refreshPreload();
}

void Player::clearQueue() {
    newSourceRequest();  // a load still in flight must not refill the cleared list
    setQueue({}, {}, false);
}

const Yandex::Track* Player::currentTrack() const {
    return (playingIndex >= 0 && playingIndex < queuedTracks.size()) ? &queuedTracks[playingIndex]
                                                                     : nullptr;
}

double Player::durationSeconds() const {
    const auto* t = currentTrack();
    return t ? double(t->durationMs) / 1000.0 : 0.0;
}

void Player::play() {
    switch (audioEngine->state()) {
        case Audio::AudioEngine::State::Paused: audioEngine->resume(); return;
        case Audio::AudioEngine::State::Playing:
            playIndex(playingIndex);
            return;  // Winamp: Play restarts the track
        case Audio::AudioEngine::State::Buffering: return;
        case Audio::AudioEngine::State::Stopped:
            playIndex(playingIndex < 0 ? 0 : playingIndex);
            return;
    }
}

void Player::pause() {
    if (audioEngine->state() == Audio::AudioEngine::State::Paused) {
        audioEngine->resume();
    } else {
        audioEngine->pause();
    }
}

void Player::closeOpenTrack() {
    if (openTrack && openTrackEvents) {
        openTrackEvents(TrackEvent::Skipped, *openTrack, accumulatePlayedSeconds());
    }
    openTrack.reset();
}

double Player::accumulatePlayedSeconds() {
    const double pos = audioEngine->positionSeconds();
    const double step = pos - lastPosition;
    // Normal progress between two polls is ~0.1 s; bigger jumps are seeks.
    if (audioEngine->state() == Audio::AudioEngine::State::Playing && step > 0 && step < 1.0) {
        played += step;
    }
    lastPosition = pos;
    return played;
}

void Player::setShuffle(bool on) {
    if (on == shuffleEnabled) {
        return;
    }
    shuffleEnabled = on;
    Q_EMIT modesChanged();
    refreshPreload();
}

void Player::setRepeat(bool on) {
    if (on == repeatEnabled) {
        return;
    }
    repeatEnabled = on;
    Q_EMIT modesChanged();
    refreshPreload();
}

void Player::stop() {
    closeOpenTrack();
    waitingForMore = false;
    ++generation;
    cancelPreload();
    abortDownload();
    audioEngine->stop();
    streamId = 0;
}

int Player::sequentialNext() const {
    if (queuedTracks.isEmpty()) {
        return -1;
    }
    const int i = playingIndex + 1;
    if (i < queuedTracks.size()) {
        return i;
    }
    if (loadMore || !repeatEnabled) {
        return -1;
    }
    return 0;
}

int Player::pickNext() const {
    if (!shuffleEnabled || queuedTracks.size() < 2) {
        return sequentialNext();
    }
    int i;
    do {
        i = int(QRandomGenerator::global()->bounded(queuedTracks.size()));
    } while (i == playingIndex);
    return i;
}

void Player::next() {
    if (queuedTracks.isEmpty()) {
        return;
    }
    if (preload) {
        return playIndex(preload->index);
    }
    const int i = pickNext();
    if (i >= 0) {
        return playIndex(i);
    }
    if (loadMore) {
        stop();
        waitingForMore = true;
        maybeLoadMore();
        Q_EMIT statusMessage(QStringLiteral("Загружаю ещё треки..."));
        return;
    }
    stop();
}

void Player::previous() {
    if (queuedTracks.isEmpty()) {
        return;
    }
    playIndex(
        playingIndex > 0 ? playingIndex - 1 : (repeatEnabled ? int(queuedTracks.size()) - 1 : 0)
    );
}

bool Player::seekFraction(double fraction) {
    const double dur = durationSeconds();
    return dur > 0 && seekTo(std::clamp(fraction, 0.0, 1.0) * dur);
}

bool Player::seekTo(double seconds) {
    const double dur = durationSeconds();
    const double target = dur > 0 ? std::clamp(seconds, 0.0, dur) : std::max(0.0, seconds);
    if (!audioEngine->seek(target)) {
        return false;
    }
    lastPosition = target;
    Q_EMIT seeked(target);
    return true;
}

void Player::maybeLoadMore() {
    if (!loadMore || loadingMore || queuedTracks.size() - playingIndex > kLoadMoreWhenLeft) {
        return;
    }
    loadingMore = true;
    const quint64 requestGeneration = queueGeneration;
    QPointer<Player> self(this);
    loadMore([self, requestGeneration](const QList<Yandex::Track>& tracks) {
        if (!self || requestGeneration != self->queueGeneration) {
            return;
        }
        self->loadingMore = false;
        const bool wasWaiting = self->waitingForMore;
        self->waitingForMore = false;
        self->appendTracks(tracks);
        if (wasWaiting && self->playingIndex + 1 < self->queuedTracks.size()) {
            self->playIndex(self->playingIndex + 1);
        }
    });
}

void Player::shutDown() {
    newSourceRequest();  // loads in flight are stale now
    stop();
    isShutDown = true;
}

void Player::playIndex(int index) {
    if (isShutDown || index < 0 || index >= queuedTracks.size()) {
        return;
    }
    waitingForMore = false;
    closeOpenTrack();
    const quint64 requestGeneration = ++generation;
    abortDownload();
    playingIndex = index;
    bitrateKbps = 0;
    currentDownloaded = false;
    downloadFailed = false;
    const Yandex::Track track = queuedTracks[index];

    if (preload && preload->stream && preload->trackId == track.id
        && preload->stream == audioEngine->queuedStream()) {
        const Preload upcoming = *std::exchange(preload, std::nullopt);
        streamId = audioEngine->playQueuedNow();
        download = upcoming.reply;
        currentDownloaded = upcoming.downloadDone && !upcoming.failed;
        downloadFailed = upcoming.failed;
        Q_EMIT currentTrackChanged();
        maybeLoadMore();
        trackStarted(track, upcoming.bitrate);
        return;
    }
    cancelPreload();
    streamId = audioEngine->beginStream();
    Q_EMIT currentTrackChanged();
    maybeLoadMore();

    yandexLibrary->api()->resolveTrackUrl(
        track.id,
        [this, requestGeneration, track](const Yandex::ResolvedUrl& url, const QString& error) {
            if (requestGeneration != generation) {
                return;
            }
            if (!error.isEmpty()) {
                Q_EMIT statusMessage(QStringLiteral("Cannot get link: ") + error);
                audioEngine->stop();
                return;
            }
            download = startDownload(url.url, streamId);
            trackStarted(track, url.bitrateKbps);
        }
    );
}

void Player::trackStarted(const Yandex::Track& track, int bitrate) {
    bitrateKbps = bitrate;
    yandexLibrary->api()->reportPlayStarted(
        yandexLibrary->account(), track, QUuid::createUuid().toString(QUuid::WithoutBraces)
    );
    openTrack = track;
    openTrackEvents = reportEvent;
    played = 0;
    lastPosition = 0;
    if (reportEvent) {
        reportEvent(TrackEvent::Started, track, 0);
    }
    Q_EMIT currentTrackChanged();
    maybePreload();
}

QNetworkReply* Player::startDownload(const QUrl& url, TStreamId stream) {
    QNetworkRequest req(url);
    req.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy
    );
    req.setTransferTimeout(kDownloadTimeoutMs);
    QNetworkReply* reply = yandexLibrary->api()->network()->get(req);
    // The engine ignores data for streams it has dropped meanwhile.
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, stream] {
        audioEngine->appendData(stream, reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, stream] {
        reply->deleteLater();
        const bool failed = reply->error() != QNetworkReply::NoError;
        if (failed) {
            audioEngine->failData(stream);
        } else {
            audioEngine->appendData(stream, reply->readAll());
            audioEngine->finishData(stream);
        }
        downloadFinished(stream, failed, reply->errorString());
    });
    return reply;
}

void Player::downloadFinished(TStreamId stream, bool failed, const QString& error) {
    if (stream && stream == streamId) {
        if (failed) {
            Q_EMIT statusMessage(QStringLiteral("Download failed: ") + error);
            downloadFailed = true;
            return;
        }
        currentDownloaded = true;
        maybePreload();
    } else if (preload && preload->stream == stream) {
        preload->downloadDone = true;
        preload->failed = failed;
    }
}

void Player::abortDownload() {
    if (QNetworkReply* reply = download) {
        download.clear();
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
}

int Player::preloadedIndex() const {
    return preload && preload->stream ? preload->index : -1;
}

void Player::maybePreload() {
    if (preload || isShutDown || !currentDownloaded || !openTrack
        || audioEngine->state() == Audio::AudioEngine::State::Stopped) {
        return;
    }
    const int index = pickNext();
    if (index < 0) {
        return;
    }
    Preload upcoming;
    upcoming.index = index;
    upcoming.trackId = queuedTracks[index].id;
    upcoming.generation = ++preloadGeneration;
    preload = upcoming;
    QPointer<Player> self(this);
    yandexLibrary->api()->resolveTrackUrl(
        upcoming.trackId,
        [self,
         requestGeneration =
             upcoming.generation](const Yandex::ResolvedUrl& url, const QString& error) {
            if (!self || !self->preload || self->preload->generation != requestGeneration) {
                return;
            }
            const TStreamId stream = error.isEmpty() ? self->audioEngine->queueStream() : 0;
            if (!stream) {
                self->preload.reset();
                return;
            }
            self->preload->stream = stream;
            self->preload->bitrate = url.bitrateKbps;
            self->preload->reply = self->startDownload(url.url, stream);
        }
    );
}

void Player::cancelPreload() {
    if (!preload) {
        return;
    }
    const Preload upcoming = *std::exchange(preload, std::nullopt);
    if (QNetworkReply* reply = upcoming.reply) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    if (upcoming.stream && upcoming.stream == audioEngine->queuedStream()) {
        audioEngine->clearQueued();
    }
}

void Player::refreshPreload() {
    if (preload) {
        int index = -1;
        if (shuffleEnabled) {
            for (int i = 0; i < queuedTracks.size() && index < 0; ++i) {
                if (i != playingIndex && queuedTracks[i].id == preload->trackId) {
                    index = i;
                }
            }
        } else if (const int i = sequentialNext();
                   i >= 0 && queuedTracks[i].id == preload->trackId) {
            index = i;
        }
        if (index >= 0) {
            preload->index = index;
            return;
        }
        cancelPreload();
    }
    maybePreload();
}

}  // namespace Core
