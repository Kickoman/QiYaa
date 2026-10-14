#include "core/player.h"

#include <QCoreApplication>
#include <QPointer>

#include <algorithm>
#include <functional>
#include <utility>
#include <variant>

namespace Core {

using Audio::AudioEngine;
namespace {
constexpr int kLoadMoreWhenLeft = 2;

QString ShuffleOffInWaveText() {
    return QCoreApplication::translate("Core::Player", "Shuffle does not apply to a vibe");
}
}  // namespace

Player::Player(Audio::AudioEngine* engine, SourceRegistry* sources, QObject* parent)
    : QObject(parent)
    , audioEngine(engine)
    , trackSources(sources) {
    connect(audioEngine, &AudioEngine::trackFinished, this, &Player::engineFinished);
    connect(audioEngine, &AudioEngine::trackAdvanced, this, &Player::engineAdvanced);
    connect(audioEngine, &AudioEngine::errorOccurred, this, [this](const QString& message) {
        Q_EMIT statusMessage(tr("Audio error: %1").arg(message));
    });
    connect(audioEngine, &AudioEngine::streamUndecodable, this, [this](TStreamId stream) {
        if (!current || !stream || stream != current->feed.stream) {
            return;
        }
        handleFailure(  // ERR-04
            current->feed.failure == FailureKind::Network ? FailureKind::Network : FailureKind::Track,
            tr("the sound could not be decoded")
        );
    });
    connect(trackSources, &SourceRegistry::filesChanged, this, &Player::sourceFilesChanged);
    retryTimer.setSingleShot(true);
    connect(&retryTimer, &QTimer::timeout, this, &Player::retryAfterNetwork);
    pollTimer.setInterval(100);
    connect(&pollTimer, &QTimer::timeout, this, [this] {
        audioEngine->poll();
        accumulatePlayedSeconds();
        Q_EMIT positionTick();
    });
    stateTimer.setSingleShot(true);
    stateTimer.setInterval(0);
    connect(&stateTimer, &QTimer::timeout, this, &Player::reportState);
    connect(audioEngine, &AudioEngine::stateChanged, this, [this](AudioEngine::State state) {
        if (state == AudioEngine::State::Playing) {
            failuresInRow = 0;  // ERR-06
        }
        if (state == AudioEngine::State::Stopped) {
            pollTimer.stop();
        } else if (!pollTimer.isActive()) {
            pollTimer.start();
        }
        noteState();
    });
}

void Player::setQueue(
    const QList<Track>& tracks,
    const QString& title,
    bool autoplay,
    TLoadMoreCallback more,
    TEventCallback events
) {
    stop();
    reportEvent = std::move(events);
    queuedTracks.clear();
    for (const Track& track : tracks) {
        if (track.available) {
            queuedTracks << track;
        }
    }
    titleText = title;
    loadMore = std::move(more);
    queueRules = {};
    loadingMore = false;
    setWaitingForMore(false);
    failuresInRow = 0;  // ERR-06
    ++queueGeneration;
    playingIndex = queuedTracks.isEmpty() ? -1 : 0;
    resetNavigator();
    Q_EMIT queueReplaced();
    Q_EMIT playlistChanged();
    Q_EMIT currentTrackChanged();
    if (loadMore && shuffleEnabled) {
        Q_EMIT statusMessage(ShuffleOffInWaveText());
    }
    if (autoplay && playingIndex >= 0) {
        playIndex(0);
    }
}

void Player::appendTracks(const QList<Track>& tracks) {
    bool added = false;
    for (const Track& track : tracks) {
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
    resetNavigator();
    noteState();
    Q_EMIT playlistChanged();
    refreshPreload();
}

void Player::insertTracks(int index, const QList<Track>& tracks) {
    QList<Track> added;
    for (const Track& track : tracks) {
        if (track.available) {
            added << track;
        }
    }
    if (added.isEmpty()) {
        return;
    }
    const int at = std::clamp(index, 0, static_cast<int>(queuedTracks.size()));
    for (qsizetype i = 0; i < added.size(); ++i) {
        queuedTracks.insert(at + i, added[i]);
    }
    if (playingIndex < 0) {
        playingIndex = 0;
    } else if (at <= playingIndex) {
        playingIndex += static_cast<int>(added.size());
    }
    resetNavigator();
    noteState();
    Q_EMIT playlistChanged();
    refreshPreload();
}

void Player::changeSource(const QString& title, TLoadMoreCallback more, TEventCallback events, QueueRules rules) {
    titleText = title;
    loadMore = std::move(more);
    reportEvent = std::move(events);
    queueRules = rules;
    loadingMore = false;
    setWaitingForMore(false);
    ++queueGeneration;
    resetNavigator();
    Q_EMIT playlistChanged();
    Q_EMIT modesChanged();
    refreshPreload();
}

void Player::removeTracks(QList<int> indices) {
    std::sort(indices.begin(), indices.end(), std::greater<>());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    bool removedCurrent = false;
    for (int index : indices) {
        if (index < 0 || index >= queuedTracks.size()) {
            continue;
        }
        queuedTracks.removeAt(index);
        if (index == playingIndex) {
            removedCurrent = true;
        } else if (index < playingIndex) {
            --playingIndex;
        }
    }
    if (removedCurrent) {
        stop();
        playingIndex = std::min<int>(playingIndex, static_cast<int>(queuedTracks.size()) - 1);
        Q_EMIT currentTrackChanged();
    }
    if (queuedTracks.isEmpty()) {
        playingIndex = -1;
    }
    resetNavigator();
    noteState();
    Q_EMIT playlistChanged();
    refreshPreload();
}

void Player::clearQueue() {
    newSourceRequest();  // a load still in flight must not refill the cleared list
    setQueue({}, {}, false);
}

const Track* Player::currentTrack() const {
    return (playingIndex >= 0 && playingIndex < queuedTracks.size()) ? &queuedTracks[playingIndex] : nullptr;
}

double Player::durationSeconds() const {
    const auto* track = currentTrack();
    return track ? static_cast<double>(track->durationMs) / 1000.0 : 0.0;
}

int Player::currentBitrate() const {
    return current ? current->bitrate : 0;
}

QUrl Player::currentLink() const {
    return current ? current->link : QUrl();
}

QUrl Player::nextLink() const {
    return preload ? preload->link : QUrl();
}

Player::State Player::state() const {
    if (current) {
        if (current->networkWait) {
            return State::WaitingForNetwork;
        }
        switch (audioEngine->state()) {
            case AudioEngine::State::Playing: return State::Playing;
            case AudioEngine::State::Paused: return State::Paused;
            case AudioEngine::State::Buffering:
            case AudioEngine::State::Stopped: break;
        }
        return State::Loading;
    }
    if (waitingForMore) {
        return State::WaitingForMore;
    }
    switch (audioEngine->state()) {
        case AudioEngine::State::Playing: return State::Playing;
        case AudioEngine::State::Paused: return State::Paused;
        case AudioEngine::State::Buffering: return State::Loading;
        case AudioEngine::State::Stopped: break;
    }
    return queuedTracks.isEmpty() ? State::Empty : State::Stopped;
}

double Player::positionSeconds() const {
    return audioEngine->positionSeconds();
}

void Player::noteState() {
    if (!stateTimer.isActive()) {
        stateTimer.start();
    }
}

void Player::reportState() {
    const State now = state();
    if (now == reportedState) {
        return;
    }
    reportedState = now;
    Q_EMIT stateChanged(now);
}

void Player::setWaitingForMore(bool waiting) {
    waitingForMore = waiting;
    noteState();
}

void Player::play() {
    if (current && current->networkWait) {
        current->networkWait->resume = true;
        return retryAfterNetwork();
    }
    switch (audioEngine->state()) {
        case AudioEngine::State::Paused: audioEngine->resume(); return;
        case AudioEngine::State::Playing: playIndex(playingIndex); return;  // Winamp: Play restarts the track
        case AudioEngine::State::Buffering: return;
        case AudioEngine::State::Stopped: playIndex(playingIndex < 0 ? 0 : playingIndex); return;
    }
}

void Player::pause() {
    if (current && current->networkWait) {
        current->networkWait->resume = !current->networkWait->resume;
        return;
    }
    if (audioEngine->state() == AudioEngine::State::Paused) {
        audioEngine->resume();
    } else {
        audioEngine->pause();
    }
}

Player::TrackEvent Player::endOfCurrent() const {
    return current && current->feed.failure ? TrackEvent::Skipped : TrackEvent::Finished;
}

void Player::closeCurrent(TrackEvent event) {
    ++generation;
    retryTimer.stop();
    if (!current) {
        return;
    }
    const double played = accumulatePlayedSeconds();
    const CurrentTrack closed = std::move(*current);
    current.reset();
    noteState();
    if (!closed.started) {
        return;
    }
    if (closed.reportToSource) {
        trackSources->trackEvent(event, closed.track, played);
    }
    if (closed.events) {
        closed.events(event, closed.track, played);
    }
}

double Player::accumulatePlayedSeconds() {
    if (!current) {
        return 0;
    }
    const double position = audioEngine->positionSeconds();
    const double step = position - current->lastPosition;
    if (audioEngine->state() == AudioEngine::State::Playing && step > 0 && step < 1.0) {
        current->playedSeconds += step;
    }
    current->lastPosition = position;
    return current->playedSeconds;
}

void Player::setShuffle(bool on) {
    if (on == shuffleEnabled) {
        return;
    }
    shuffleEnabled = on;
    resetNavigator();
    Q_EMIT modesChanged();
    if (on && loadMore) {
        Q_EMIT statusMessage(ShuffleOffInWaveText());
    }
    refreshPreload();
}

void Player::setShuffleAlgorithm(ShuffleAlgorithm algorithm) {
    if (preferredShuffleAlgorithm == algorithm) {
        return;
    }
    preferredShuffleAlgorithm = algorithm;
    if (shuffleActive()) {
        resetNavigator();
        refreshPreload();
    }
    Q_EMIT modesChanged();
}

void Player::setRepeat(bool on) {
    if (on == repeatEnabled) {
        return;
    }
    repeatEnabled = on;
    Q_EMIT modesChanged();
    refreshPreload();
}

void Player::sourceFilesChanged(const QString& source) {
    if (preload && preload->key.source != source) {
        return;
    }
    cancelPreload();
    maybePreload();
}

void Player::stop() {
    closeCurrent(TrackEvent::Skipped);
    setWaitingForMore(false);
    cancelPreload();
    audioEngine->stop();
}

void Player::resetNavigator() {
    navigator = MakeTrackNavigator(shuffleActive() ? std::optional(preferredShuffleAlgorithm) : std::nullopt);
    navigator->reset(static_cast<int>(queuedTracks.size()), playingIndex);
}

int Player::pickNext() const {
    return navigator->next(repeatEnabled && !loadMore);
}

void Player::next() {
    if (queuedTracks.isEmpty()) {
        return;
    }
    if (preload) {
        return playIndex(preload->index);
    }
    const int nextIndex = pickNext();
    if (nextIndex >= 0) {
        return playIndex(nextIndex);
    }
    if (loadMore) {
        stop();
        setWaitingForMore(true);
        maybeLoadMore();
        Q_EMIT statusMessage(tr("Loading more tracks…"));
        return;
    }
    stop();
}

int PreviousTarget(double positionSeconds, int cursor, int size, bool repeat) {
    if (positionSeconds > kPreviousRestartsAfterSeconds) {
        return -1;
    }
    if (cursor > 0) {
        return cursor - 1;
    }
    return repeat ? size - 1 : 0;
}

void Player::previous() {
    if (queuedTracks.isEmpty()) {
        return;
    }
    if (queueRules.previousRestartsOnly) {  // HOST-07
        if (!seekTo(0)) {
            playIndex(playingIndex);
        }
        return;
    }
    const bool active = audioEngine->state() != AudioEngine::State::Stopped;
    const int target = active && audioEngine->positionSeconds() > kPreviousRestartsAfterSeconds
        ? -1
        : navigator->previous(repeatEnabled);
    if (target >= 0) {
        playIndex(target);
    } else if (!seekTo(0)) {
        playIndex(playingIndex);
    }
}

bool Player::seekFraction(double fraction) {
    const double duration = durationSeconds();
    return duration > 0 && seekTo(std::clamp(fraction, 0.0, 1.0) * duration);
}

bool Player::seekTo(double seconds) {
    const double duration = durationSeconds();
    const double target = duration > 0 ? std::clamp(seconds, 0.0, duration) : std::max(0.0, seconds);
    if (!audioEngine->seek(target)) {
        return false;
    }
    if (current) {
        current->lastPosition = target;
    }
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
    loadMore([self, requestGeneration](const QList<Track>& tracks) {
        if (!self || requestGeneration != self->queueGeneration) {
            return;
        }
        self->loadingMore = false;
        const bool wasWaiting = self->waitingForMore;
        self->setWaitingForMore(false);
        self->appendTracks(tracks);
        if (wasWaiting) {
            if (self->playingIndex + 1 < self->queuedTracks.size()) {
                self->playIndex(self->playingIndex + 1);
            } else {
                self->setWaitingForMore(true);
            }
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
    setWaitingForMore(false);
    closeCurrent(TrackEvent::Skipped);
    playingIndex = index;
    navigator->select(index);
    const Track& track = queuedTracks[index];
    if (preload && preload->feed.stream && preload->key == track.key
        && preload->feed.stream == audioEngine->queuedStream()) {
        audioEngine->playQueuedNow();
        return adoptPreload(index);
    }
    cancelPreload();
    if (!audioEngine->hasOutput()) {
        Q_EMIT currentTrackChanged();
        maybeLoadMore();
        Q_EMIT statusMessage(tr("No audio output device"));
        return;
    }
    current.emplace();
    current->track = track;
    current->feed.stream = audioEngine->beginStream();
    noteState();
    Q_EMIT currentTrackChanged();
    maybeLoadMore();
    openCurrent();
}

void Player::adoptPreload(int index) {
    Preload upcoming = std::move(*preload);
    preload.reset();
    ++generation;
    playingIndex = index;
    navigator->select(index);
    current.emplace();
    current->track = queuedTracks[index];
    current->link = upcoming.link;
    current->feed = std::move(upcoming.feed);
    setWaitingForMore(false);
    Q_EMIT currentTrackChanged();
    maybeLoadMore();
    startCurrent(upcoming.bitrate);
}

void Player::openCurrent() {
    const quint64 requestGeneration = ++generation;
    trackSources->open(current->track, [this, self = QPointer<Player>(this), requestGeneration](TOpenResult result) {
        if (!self || requestGeneration != generation || !current) {
            return;
        }
        if (const auto* failure = std::get_if<TrackFailure>(&result)) {
            return handleFailure(failure->kind, failure->text);
        }
        OpenedTrack& opened = std::get<OpenedTrack>(result);
        const std::optional<NetworkWait> wait = std::exchange(current->networkWait, std::nullopt);
        noteState();
        current->link = opened.link;
        startFeed(current->feed, std::move(opened.download));
        if (current->started) {
            current->bitrate = opened.bitrateKbps;
            Q_EMIT currentTrackChanged();
        } else {
            startCurrent(opened.bitrateKbps);
        }
        if (wait && wait->resume) {
            audioEngine->resume();
        }
    });
}

void Player::startCurrent(int bitrate) {
    current->bitrate = bitrate;
    current->started = true;
    current->events = reportEvent;
    current->reportToSource = queueRules.playReports;
    current->playedSeconds = 0;
    current->lastPosition = 0;
    const Track track = current->track;
    const TEventCallback events = current->events;
    if (current->reportToSource) {
        trackSources->trackEvent(TrackEvent::Started, track, 0);
    }
    if (events) {
        events(TrackEvent::Started, track, 0);
    }
    Q_EMIT currentTrackChanged();
    maybePreload();
}

void Player::startFeed(Feed& feed, std::unique_ptr<TrackDownload> download) {
    feed.download = std::move(download);
    feed.state = DownloadState::Running;
    TrackDownload* started = feed.download.get();
    const TStreamId stream = feed.stream;
    connect(started, &TrackDownload::received, this, [this, stream](const QByteArray& bytes) {
        audioEngine->appendData(stream, bytes);
        if (Feed* fed = feedOf(stream); fed && !bytes.isEmpty()) {
            fed->audioArrived = true;
        }
    });
    connect(
        started,
        &TrackDownload::finished,
        this,
        [this, started, stream](const std::optional<TrackFailure>& failure) {
            if (failure) {
                audioEngine->failData(stream);
            } else {
                audioEngine->finishData(stream);
            }
            downloadFinished(started, failure);
        }
    );
    started->start();
}

Player::Feed* Player::feedOf(TStreamId stream) {
    if (current && current->feed.stream == stream) {
        return &current->feed;
    }
    if (preload && preload->feed.stream == stream) {
        return &preload->feed;
    }
    return nullptr;
}

void Player::downloadFinished(TrackDownload* finished, const std::optional<TrackFailure>& failure) {
    if (current && current->feed.download.get() == finished) {
        current->feed.download.release()->deleteLater();
        current->feed.state = DownloadState::Finished;
        if (!failure) {
            return maybePreload();
        }
        if (!current->feed.audioArrived) {
            return handleFailure(failure->kind, failure->text);
        }
        current->feed.failure = failure->kind;
        if (failure->kind != FailureKind::Network) {
            Q_EMIT statusMessage(tr("Download failed: %1").arg(failure->text));
        }
        return;
    }
    if (preload && preload->feed.download.get() == finished) {
        preload->feed.download.release()->deleteLater();
        if (failure) {
            return cancelPreload();
        }
        preload->feed.state = DownloadState::Finished;
    }
}

void Player::engineFinished() {
    if (current && current->feed.failure == FailureKind::Network) {
        return handleFailure(FailureKind::Network, {});  // ERR-03
    }
    closeCurrent(endOfCurrent());
    if (repeatEnabled && queuedTracks.size() == 1 && !loadMore) {  // WAVE-12
        return playIndex(playingIndex);
    }
    next();
}

void Player::engineAdvanced() {
    closeCurrent(endOfCurrent());
    if (!preload || preload->feed.stream != audioEngine->currentStream() || preload->index >= queuedTracks.size()) {
        const int nextIndex = pickNext();
        nextIndex >= 0 ? playIndex(nextIndex) : stop();
        return;
    }
    adoptPreload(preload->index);
}

void Player::handleFailure(FailureKind kind, const QString& text) {
    Q_EMIT failed(kind);
    const FailureAction action = DecideOnFailure(kind, failuresInRow, pickNext() >= 0, static_cast<bool>(loadMore));
    switch (action) {
        case FailureAction::WaitForNetwork: return waitForNetwork();
        case FailureAction::Next:  // ERR-04, ERR-07
            ++failuresInRow;
            Q_EMIT statusMessage(tr("The track does not play: %1").arg(text));
            return next();
        case FailureAction::Stop:
            if (kind == FailureKind::Auth) {  // ERR-08
                Q_EMIT statusMessage(tr("Access error: %1").arg(text));
            } else {  // ERR-07
                ++failuresInRow;
                Q_EMIT statusMessage(tr("The track does not play: %1").arg(text));
            }
            return stop();
        case FailureAction::StopAfterLimit:  // ERR-05
            failuresInRow = 0;
            Q_EMIT statusMessage(tr("Stopped: %n track(s) in a row did not play", nullptr, kMaxTrackFailuresInRow));
            return stop();
    }
}

void Player::waitForNetwork() {
    if (!current) {
        return stop();
    }
    NetworkWait wait;
    if (current->networkWait) {
        wait = *current->networkWait;
        ++wait.attempt;
    } else {
        const AudioEngine::State state = audioEngine->state();
        wait.resume = state != AudioEngine::State::Paused;
        wait.position = state == AudioEngine::State::Stopped ? current->lastPosition : audioEngine->positionSeconds();
        Q_EMIT statusMessage(tr("No network — waiting for it…"));
    }
    ++generation;
    cancelPreload();
    current->feed = Feed{};
    current->feed.stream = audioEngine->beginStream(wait.position);
    if (!current->feed.stream) {
        return stop();
    }
    audioEngine->pause();
    current->lastPosition = wait.position;
    current->networkWait = wait;
    noteState();
    retryTimer.stop();
    if (networkOnline != false) {
        retryTimer.start(std::min(retryMaxMs, retryFirstMs << std::min(wait.attempt, 16)));
    }
}

void Player::retryAfterNetwork() {
    if (!current || !current->networkWait) {
        return;
    }
    retryTimer.stop();
    openCurrent();
}

void Player::setNetworkOnline(std::optional<bool> online) {
    networkOnline = online;
    if (!current || !current->networkWait) {
        return;
    }
    if (online == true) {
        current->networkWait->attempt = 0;
        retryAfterNetwork();
    } else if (online == false) {
        retryTimer.stop();
    }
}

void Player::setNetworkRetryDelays(int firstMs, int maxMs) {
    retryFirstMs = std::max(1, firstMs);
    retryMaxMs = std::max(retryFirstMs, maxMs);
}

int Player::preloadedIndex() const {
    return preload && preload->feed.stream ? preload->index : -1;
}

void Player::maybePreload() {
    if (preload || isShutDown || !current || !current->started || current->feed.state != DownloadState::Finished
        || current->feed.failure || audioEngine->state() == AudioEngine::State::Stopped) {
        return;
    }
    const int index = pickNext();
    if (index < 0) {
        return;
    }
    preload.emplace();
    preload->index = index;
    preload->key = queuedTracks[index].key;
    preload->generation = ++preloadGeneration;
    QPointer<Player> self(this);
    trackSources->open(queuedTracks[index], [self, requestGeneration = preload->generation](TOpenResult result) {
        if (!self || !self->preload || self->preload->generation != requestGeneration) {
            return;
        }
        auto* opened = std::get_if<OpenedTrack>(&result);
        const TStreamId stream = opened ? self->audioEngine->queueStream() : 0;
        if (!stream) {
            self->preload.reset();
            return;
        }
        self->preload->feed.stream = stream;
        self->preload->bitrate = opened->bitrateKbps;
        self->preload->link = opened->link;
        self->startFeed(self->preload->feed, std::move(opened->download));
    });
}

void Player::cancelPreload() {
    if (!preload) {
        return;
    }
    const TStreamId stream = preload->feed.stream;
    preload.reset();
    if (stream && stream == audioEngine->queuedStream()) {
        audioEngine->clearQueued();
    }
}

void Player::refreshPreload() {
    if (preload) {
        if (const int nextIndex = pickNext(); nextIndex >= 0 && queuedTracks[nextIndex].key == preload->key) {
            preload->index = nextIndex;
            return;
        }
        cancelPreload();
    }
    maybePreload();
}

}  // namespace Core
