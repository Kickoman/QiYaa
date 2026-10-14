#include "core/player.h"

#include "core/queue.h"

#include <QPointer>
#include <QStringList>

#include <algorithm>
#include <functional>
#include <utility>
#include <variant>

namespace Core {

using Audio::AudioEngine;
namespace {
constexpr int kLoadMoreWhenLeft = 2;
constexpr qsizetype kQueueEndSize = 5;

PlayerNotice NoticeOf(NoticeCode code, const QString& detail = {}, int count = 0, bool undecodable = false) {
    PlayerNotice notice;
    notice.code = code;
    notice.detail = detail;
    notice.count = count;
    notice.undecodable = undecodable;
    return notice;
}
}  // namespace

Player::Player(Audio::AudioEngine* engine, SourceRegistry* sources, QObject* parent)
    : QObject(parent)
    , audioEngine(engine)
    , trackSources(sources)
    , queue(std::make_unique<Queue>()) {
    connect(audioEngine, &AudioEngine::trackFinished, this, &Player::engineFinished);
    connect(audioEngine, &AudioEngine::trackAdvanced, this, &Player::engineAdvanced);
    connect(audioEngine, &AudioEngine::errorOccurred, this, [this](const QString& message) {
        Q_EMIT noticed(NoticeOf(NoticeCode::AudioError, message));
    });
    connect(audioEngine, &AudioEngine::streamUndecodable, this, [this](TStreamId stream) {
        if (!current || !stream || stream != current->feed.stream) {
            return;
        }
        handleFailure(  // ERR-04
            current->feed.failure == FailureKind::Network ? FailureKind::Network : FailureKind::Track,
            {},
            true
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

Player::~Player() = default;

void Player::setSource(QueueSource source, bool autoplay) {
    if (layer) {
        const int cursor = queue->cursor();
        queue->replaceBase(std::move(source));
        takeNewBase();
        setWaitingForMore(false);
        Q_EMIT queueReplaced();
        Q_EMIT playlistChanged();
        if (queue->cursor() != cursor) {
            Q_EMIT currentTrackChanged();
        }
        Q_EMIT modesChanged();
        if (queue->continuation()) {
            queue->continuation()->begin(queueRules.ownContinuationFeedback);  // HOST-15
        }
        refreshPreload();
        return;
    }
    stop();
    queue->replace(std::move(source));
    takeNewBase();
    setWaitingForMore(false);
    failuresInRow = 0;  // ERR-06
    Q_EMIT queueReplaced();
    Q_EMIT playlistChanged();
    Q_EMIT currentTrackChanged();
    if (queue->continuation()) {
        if (queue->shuffle()) {
            Q_EMIT noticed(NoticeOf(NoticeCode::ShuffleOffInVibe));
        }
        queue->continuation()->begin(true);  // TRK-03
    }
    if (autoplay && queue->cursor() >= 0) {
        playIndex(0);
    }
}

void Player::removeRows(QList<int> rows) {
    if (layer) {
        QStringList items;
        QList<int> others;
        for (int row : rows) {
            const bool waiting = row >= 0 && row < queue->entries().size() && queue->entries()[row].pending;
            if (waiting) {
                items << queue->entries()[row].itemId;  // HOST-36: the server removes it
            } else {
                others << row;
            }
        }
        if (!items.isEmpty() && layer->removeItems) {
            layer->removeItems(items);
        }
        rows = others;
    }
    if (queue->remove(std::move(rows))) {
        stop();
        Q_EMIT currentTrackChanged();
    }
    noteState();
    Q_EMIT playlistChanged();
    refreshPreload();
}

void Player::clear() {
    newSourceRequest();  // a load still in flight must not refill the cleared list
    setSource({}, false);
}

void Player::setLayer(std::optional<QueueLayer> newLayer) {
    if (!layer && !newLayer) {
        return;
    }
    const quint64 revision = queue->revision();
    const bool ending = layer && !newLayer;
    layer = std::move(newLayer);
    queueRules = layer ? layer->rules : QueueRules{};
    if (ending) {
        queue->endLayer();  // HOST-32
        for (const std::shared_ptr<Continuation>& begun : {queue->continuation(), queue->historyWave()}) {
            if (begun) {
                begun->begin(true);  // HOST-33: what began without feedback during the jam
            }
        }
    }
    Q_EMIT modesChanged();
    queueEdited(revision);
}

void Player::setLayerItems(const QList<LayerItem>& items, std::optional<SeedHint> hint) {
    if (!layer) {
        return;
    }
    const quint64 revision = queue->revision();
    queue->setItems(items, std::move(hint));
    queueEdited(revision);  // HOST-04: no change, no signal
    const State now = state();
    if (now != State::Empty && now != State::Stopped) {
        maybeLoadMore();
    }
}

void Player::takeNewBase() {
    queue->setContinueWithVibe(continueWithVibeByDefault);  // CONT-01
    if (!queue->continuation() && historyWaveFactory) {
        queue->setHistoryWave(historyWaveFactory());
    }
}

void Player::setHistoryWaveFactory(THistoryWaveFactory factory) {
    historyWaveFactory = std::move(factory);
    if (!queue->continuation() && !queue->historyWave() && historyWaveFactory) {
        queue->setHistoryWave(historyWaveFactory());
    }
}

void Player::setContinueWithVibe(bool on) {
    if (on == queue->continuesWithVibe()) {
        return;
    }
    queue->setContinueWithVibe(on);
    if (on) {
        queue->setStalled(false);  // CONT-02, CONT-03
    }
    Q_EMIT modesChanged();
    if (on) {
        maybeLoadMore();
    }
}

bool Player::continuesWithVibe() const {
    return queue->continuesWithVibe();
}

bool Player::hasOwnContinuation() const {
    return static_cast<bool>(queue->continuation());
}

void Player::queueEdited(quint64 revision) {
    if (queue->revision() == revision) {
        return;
    }
    noteState();
    Q_EMIT playlistChanged();
    refreshPreload();
}

const QList<QueueEntry>& Player::entries() const {
    return queue->entries();
}

const QString& Player::queueTitle() const {
    return queue->title();
}

int Player::currentIndex() const {
    return queue->cursor();
}

const QueueEntry* Player::currentEntry() const {
    return queue->currentEntry();
}

bool Player::hasNext() const {
    return queue->next() >= 0;
}

ShuffleAlgorithm Player::shuffleAlgorithm() const {
    return queue->shuffleAlgorithm();
}

bool Player::shuffle() const {
    return queue->shuffle();
}

bool Player::shuffleActive() const {
    return queue->shuffleActive();
}

bool Player::repeat() const {
    return queue->repeat();
}

const Track* Player::currentTrack() const {
    const QueueEntry* entry = queue->currentEntry();
    return entry ? &entry->track : nullptr;
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
    return queue->entries().isEmpty() ? State::Empty : State::Stopped;
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
        case AudioEngine::State::Playing: playIndex(queue->cursor()); return;  // Winamp: Play restarts the track
        case AudioEngine::State::Buffering: return;
        case AudioEngine::State::Stopped: playIndex(std::max(0, queue->cursor())); return;
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
    if (closed.giver) {
        closed.giver->trackEvent(event, closed.track, played);
    }
    Q_EMIT trackEvent(event, closed.track, played);
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
    if (on == queue->shuffle()) {
        return;
    }
    queue->setShuffle(on);
    Q_EMIT modesChanged();
    if (on && queue->continuation()) {
        Q_EMIT noticed(NoticeOf(NoticeCode::ShuffleOffInVibe));
    }
    refreshPreload();
}

void Player::setShuffleAlgorithm(ShuffleAlgorithm algorithm) {
    if (queue->shuffleAlgorithm() == algorithm) {
        return;
    }
    queue->setShuffleAlgorithm(algorithm);
    if (queue->shuffleActive()) {
        refreshPreload();
    }
    Q_EMIT modesChanged();
}

void Player::setRepeat(bool on) {
    if (on == queue->repeat()) {
        return;
    }
    queue->setRepeat(on);
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

void Player::next() {
    advance(true);
}

void Player::advance(bool byUser) {
    if (queue->entries().isEmpty()) {
        return;
    }
    if (preload) {
        return playIndex(preload->index);
    }
    const int nextIndex = queue->next();
    if (nextIndex >= 0) {
        return playIndex(nextIndex);
    }
    stop();
    if (!askedContinuation()) {
        return;
    }
    if (byUser) {
        queue->setStalled(false);  // CONT-07, WAVE-09: Next asks again
    }
    setWaitingForMore(true);
    maybeLoadMore();
    if (queue->isAsking()) {
        Q_EMIT noticed(NoticeOf(NoticeCode::LoadingMore));
    } else if (waitingForMore) {
        setWaitingForMore(false);  // CONT-06: nothing to ask with
    }
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
    if (queue->entries().isEmpty()) {
        return;
    }
    if (queueRules.previousRestartsOnly) {  // HOST-07
        if (!seekTo(0)) {
            playIndex(queue->cursor());
        }
        return;
    }
    const bool active = audioEngine->state() != AudioEngine::State::Stopped;
    const int target =
        active && audioEngine->positionSeconds() > kPreviousRestartsAfterSeconds ? -1 : queue->previous();
    if (target >= 0) {
        playIndex(target);
    } else if (!seekTo(0)) {
        playIndex(queue->cursor());
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

std::shared_ptr<Continuation> Player::askedContinuation() const {
    if (queue->continuation()) {
        return queue->continuation();
    }
    return queue->continuesWithVibe() ? queue->historyWave() : nullptr;
}

void Player::maybeLoadMore() {
    const std::shared_ptr<Continuation> continuation = askedContinuation();
    if (!continuation || queue->isAsking() || queue->isStalled() || queue->remaining() > kLoadMoreWhenLeft) {
        return;
    }
    queue->setAsking(true);
    const bool history = continuation == queue->historyWave();
    ContinuationRequest request;
    request.history = queue->history();
    request.jamSeeds = layer ? queue->seeds() : std::nullopt;  // HOST-11
    request.feedback = history ? queueRules.historyFeedback : queueRules.ownContinuationFeedback;
    const QList<QueueEntry>& entries = queue->entries();
    for (qsizetype i = std::max<qsizetype>(0, entries.size() - kQueueEndSize); i < entries.size(); ++i) {
        request.queueEnd << entries[i].track;
    }
    const quint64 requestGeneration = queue->generation();
    QPointer<Player> self(this);
    const bool asked =
        continuation->more(request, [self, requestGeneration, continuation](const ContinuationReply& reply) {
            if (self && requestGeneration == self->queue->generation()) {
                self->continuationReplied(continuation, reply);
            }
        });
    if (!asked) {
        queue->setAsking(false);
    }
}

void Player::continuationReplied(const std::shared_ptr<Continuation>& giver, const ContinuationReply& reply) {
    queue->setAsking(false);
    if (giver == queue->historyWave() && queue->isStale(reply.seedsVersion)) {
        maybeLoadMore();  // HOST-11, HOST-32: a session that the seeds outdated
        if (!queue->isAsking() && waitingForMore) {
            setWaitingForMore(false);
        }
        return;
    }
    if (!reply.failure.isEmpty()) {
        Q_EMIT noticed(NoticeOf(NoticeCode::VibeFailed, reply.failure));
    }
    const bool firstOfHistory = giver == queue->historyWave() && !queue->historyWaveGave();
    const bool wasWaiting = waitingForMore;
    setWaitingForMore(false);
    const quint64 revision = queue->revision();
    if (!queue->appendContinuation(reply.tracks, giver, reply.seedsVersion) && firstOfHistory) {
        queue->setStalled(true);  // CONT-07: the head plays to its end, and only Next asks again
    }
    queueEdited(revision);
    if (wasWaiting) {
        if (const int nextIndex = queue->next(); nextIndex >= 0) {
            playIndex(nextIndex);  // else stopped at the end: WAVE-09, CONT-07
        }
    }
}

void Player::shutDown() {
    newSourceRequest();  // loads in flight are stale now
    stop();
    isShutDown = true;
}

void Player::playIndex(int index) {
    if (isShutDown || index < 0 || index >= queue->entries().size()) {
        return;
    }
    setWaitingForMore(false);
    closeCurrent(TrackEvent::Skipped);
    const quint64 revision = queue->revision();
    index = queue->select(index);
    if (queue->revision() != revision) {
        Q_EMIT playlistChanged();
    }
    const Track& track = queue->entries()[index].track;
    if (preload && preload->feed.stream && preload->key == track.key
        && preload->feed.stream == audioEngine->queuedStream()) {
        audioEngine->playQueuedNow();
        return adoptPreload(index);
    }
    cancelPreload();
    if (!audioEngine->hasOutput()) {
        Q_EMIT currentTrackChanged();
        maybeLoadMore();
        Q_EMIT noticed(NoticeOf(NoticeCode::NoOutputDevice));
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
    const quint64 revision = queue->revision();
    index = queue->select(index);
    if (queue->revision() != revision) {
        Q_EMIT playlistChanged();
    }
    current.emplace();
    current->track = queue->entries()[index].track;
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
    const QueueEntry* entry = queue->currentEntry();
    const bool feedback =
        entry && (entry->fromHistory ? queueRules.historyFeedback : queueRules.ownContinuationFeedback);
    current->giver = feedback && entry->part == EntryPart::Continuation ? entry->giver : nullptr;  // HOST-15, HOST-16
    current->reportToSource = queueRules.playReports;
    current->playedSeconds = 0;
    current->lastPosition = 0;
    if (entry) {
        queue->noteStarted(*entry);
    }
    const Track track = current->track;
    const std::shared_ptr<Continuation> giver = current->giver;
    if (current->reportToSource) {
        trackSources->trackEvent(TrackEvent::Started, track, 0);
    }
    if (giver) {
        giver->trackEvent(TrackEvent::Started, track, 0);
    }
    Q_EMIT trackEvent(TrackEvent::Started, track, 0);
    Q_EMIT currentTrackChanged();
    if (const std::shared_ptr<Continuation> asked = askedContinuation(); asked && asked == queue->historyWave()) {
        maybeLoadMore();  // CONT-04: a seed comes when a track starts
    }
    maybePreload();
}

void Player::startFeed(Feed& feed, std::unique_ptr<TrackDownload> download) {
    feed.download = std::move(download);
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
        if (!failure) {
            current->feed.downloaded = true;
            return maybePreload();
        }
        if (!current->feed.audioArrived) {
            return handleFailure(failure->kind, failure->text);
        }
        current->feed.failure = failure->kind;
        if (failure->kind != FailureKind::Network) {
            Q_EMIT noticed(NoticeOf(NoticeCode::DownloadFailed, failure->text));
        }
        return;
    }
    if (preload && preload->feed.download.get() == finished) {
        preload->feed.download.release()->deleteLater();
        if (failure) {
            return cancelPreload();
        }
        preload->feed.downloaded = true;
    }
}

void Player::engineFinished() {
    if (current && current->feed.failure == FailureKind::Network) {
        return handleFailure(FailureKind::Network, {});  // ERR-03
    }
    closeCurrent(endOfCurrent());
    if (queue->cursor() >= 0 && queue->next() == queue->cursor()) {  // TR-05 with one track
        return playIndex(queue->cursor());
    }
    advance(false);
}

void Player::engineAdvanced() {
    closeCurrent(endOfCurrent());
    if (!preload || preload->feed.stream != audioEngine->currentStream() || preload->index >= queue->entries().size()) {
        const int nextIndex = queue->next();
        nextIndex >= 0 ? playIndex(nextIndex) : stop();
        return;
    }
    adoptPreload(preload->index);
}

void Player::handleFailure(FailureKind kind, const QString& text, bool undecodable) {
    Q_EMIT failed(kind);
    const FailureAction action =
        DecideOnFailure(kind, failuresInRow, queue->next() >= 0, static_cast<bool>(askedContinuation()));
    switch (action) {
        case FailureAction::WaitForNetwork: return waitForNetwork();
        case FailureAction::Next:  // ERR-04, ERR-07
            ++failuresInRow;
            Q_EMIT noticed(NoticeOf(NoticeCode::TrackFailed, text, 0, undecodable));
            return advance(false);
        case FailureAction::Stop:
            if (kind == FailureKind::Auth) {  // ERR-08
                Q_EMIT noticed(NoticeOf(NoticeCode::AccessDenied, text));
            } else {  // ERR-07
                ++failuresInRow;
                Q_EMIT noticed(NoticeOf(NoticeCode::TrackFailed, text, 0, undecodable));
            }
            return stop();
        case FailureAction::StopAfterLimit:  // ERR-05
            failuresInRow = 0;
            Q_EMIT noticed(NoticeOf(NoticeCode::StoppedAfterFailures, {}, kMaxTrackFailuresInRow));
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
        Q_EMIT noticed(NoticeOf(NoticeCode::WaitingForNetwork));
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
    if (preload || isShutDown || !current || !current->started || !current->feed.downloaded
        || audioEngine->state() == AudioEngine::State::Stopped) {
        return;
    }
    const int index = queue->next();
    if (index < 0) {
        return;
    }
    preload.emplace();
    preload->index = index;
    preload->key = queue->entries()[index].track.key;
    preload->generation = ++preloadGeneration;
    QPointer<Player> self(this);
    trackSources
        ->open(queue->entries()[index].track, [self, requestGeneration = preload->generation](TOpenResult result) {
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
        if (const int nextIndex = queue->next();
            nextIndex >= 0 && queue->entries()[nextIndex].track.key == preload->key) {
            preload->index = nextIndex;
            return;
        }
        cancelPreload();
    }
    maybePreload();
}

}  // namespace Core
