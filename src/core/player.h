#pragma once

#include "audio/audio_engine.h"
#include "core/continuation.h"
#include "core/failure_policy.h"
#include "core/queue_source.h"
#include "core/track.h"
#include "core/track_navigator.h"
#include "core/track_source.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <memory>
#include <optional>

namespace Core {

class Queue;

// Previous goes back a track only near a track's start; later it restarts the track.
inline constexpr double kPreviousRestartsAfterSeconds = 3.0;

// What Previous does at `positionSeconds` into track `cursor` of `size`: the index to play, or -1
// to restart the current track from 0 (spec TR-01, TR-02).
int PreviousTarget(double positionSeconds, int cursor, int size, bool repeat);

enum class NoticeCode {
    ShuffleOffInVibe,
    AudioError,
    NoOutputDevice,
    LoadingMore,
    DownloadFailed,
    TrackFailed,
    AccessDenied,
    StoppedAfterFailures,
    WaitingForNetwork,
    VibeFailed,
};

struct PlayerNotice {
    NoticeCode code = NoticeCode::AudioError;
    QString detail;
    int count = 0;
    bool undecodable = false;
};

class Player : public QObject {
    Q_OBJECT
public:
    using TrackEvent = Core::TrackEvent;
    enum class State { Empty, Stopped, Loading, Playing, Paused, WaitingForNetwork, WaitingForMore };
    Q_ENUM(State)

    Player(Audio::AudioEngine* engine, SourceRegistry* sources, QObject* parent = nullptr);
    ~Player() override;

    quint64 newSourceRequest() { return ++sourceRequest; }
    bool isLatestSourceRequest(quint64 ticket) const { return ticket == sourceRequest; }

    void setSource(QueueSource source, bool autoplay);
    void add(QueueSource source, AddPlacement placement);
    void removeRows(QList<int> rows);
    void clear();
    void setLayer(std::optional<QueueLayer> layer);
    void setLayerItems(const QList<LayerItem>& items, std::optional<SeedHint> hint);
    void setHistoryWaveFactory(THistoryWaveFactory factory);
    void setContinueWithVibeByDefault(bool on) { continueWithVibeByDefault = on; }
    void setContinueWithVibe(bool on);
    bool continuesWithVibe() const;
    bool hasOwnContinuation() const;
    bool canSwitchVibe() const;

    void play();
    void pause();
    void stop();
    void next();
    void previous();
    void playIndex(int index);
    void shutDown();
    bool seekTo(double seconds);
    bool seekFraction(double fraction);
    void setShuffle(bool on);
    void setShuffleAlgorithm(ShuffleAlgorithm algorithm);
    ShuffleAlgorithm shuffleAlgorithm() const;
    void setRepeat(bool on);
    bool shuffle() const;  // the user's choice
    // Shuffle as it applies now: never when the source has a continuation of its own (a vibe plays in queue order).
    bool shuffleActive() const;
    bool repeat() const;
    const QueueRules& rules() const { return queueRules; }
    State state() const;
    double positionSeconds() const;
    QUrl currentLink() const;
    QUrl nextLink() const;

    const QList<QueueEntry>& entries() const;
    const QString& queueTitle() const;
    int currentIndex() const;
    const QueueEntry* currentEntry() const;
    bool hasNext() const;
    const Track* currentTrack() const;
    int currentBitrate() const;
    double durationSeconds() const;
    int preloadedIndex() const;

    // What the system says about the network: nullopt when it cannot tell, and then only the
    // retry timer ends a wait for the network. Coming online retries a waiting track at once.
    void setNetworkOnline(std::optional<bool> online);
    // The delays between tries while waiting for the network: `firstMs`, doubling up to `maxMs`
    // (2 s and 60 s unless a test sets them).
    void setNetworkRetryDelays(int firstMs, int maxMs);

Q_SIGNALS:
    void noticed(const Core::PlayerNotice& notice);
    void failed(Core::FailureKind kind);
    void trackEvent(Core::TrackEvent event, const Core::Track& track, double playedSeconds);
    void stateChanged(Core::Player::State state);
    void playlistChanged();
    void queueReplaced();
    void currentTrackChanged();
    void positionTick();
    void modesChanged();
    void seeked(double seconds);

private:
    using TStreamId = Audio::AudioEngine::TStreamId;
    struct Feed {
        TStreamId stream = 0;
        std::unique_ptr<TrackDownload> download;
        bool downloaded = false;
        std::optional<FailureKind> failure;
        bool audioArrived = false;
    };
    struct NetworkWait {
        bool resume = true;
        double position = 0;
        int attempt = 0;
    };
    struct CurrentTrack {
        Track track;
        bool started = false;
        std::shared_ptr<Continuation> giver;
        bool reportToSource = false;
        QUrl link;
        int bitrate = 0;
        Feed feed;
        std::optional<NetworkWait> networkWait;
        double playedSeconds = 0;
        double lastPosition = 0;
    };
    struct Preload {
        int index = -1;
        TrackKey key;
        quint64 generation = 0;
        QUrl link;
        int bitrate = 0;
        Feed feed;
    };

    void engineFinished();
    void engineAdvanced();
    void openCurrent();
    void startCurrent(int bitrate);
    void adoptPreload(int index);
    void closeCurrent(TrackEvent event);
    TrackEvent endOfCurrent() const;
    void startFeed(Feed& feed, std::unique_ptr<TrackDownload> download);
    Feed* feedOf(TStreamId stream);
    void downloadFinished(TrackDownload* finished, const std::optional<TrackFailure>& failure);
    void sourceFilesChanged(const QString& source);
    void handleFailure(FailureKind kind, const QString& text, bool undecodable = false);
    void waitForNetwork();
    void retryAfterNetwork();
    void maybePreload();
    void cancelPreload();
    void refreshPreload();
    void advance(bool byUser);
    void takeNewBase();
    std::shared_ptr<Continuation> askedContinuation() const;
    void maybeLoadMore();
    void continuationReplied(const std::shared_ptr<Continuation>& giver, const ContinuationReply& reply);
    void queueEdited(quint64 revision);
    void setWaitingForMore(bool waiting);
    double accumulatePlayedSeconds();
    void noteState();
    void reportState();

    Audio::AudioEngine* audioEngine;
    SourceRegistry* trackSources;
    std::unique_ptr<Queue> queue;
    std::optional<QueueLayer> layer;
    QueueRules queueRules;
    THistoryWaveFactory historyWaveFactory;
    bool continueWithVibeByDefault = true;
    std::optional<CurrentTrack> current;
    std::optional<Preload> preload;
    int failuresInRow = 0;
    std::optional<bool> networkOnline;
    QTimer retryTimer;
    int retryFirstMs = 2'000;
    int retryMaxMs = 60'000;
    bool isShutDown = false;
    bool waitingForMore = false;
    quint64 sourceRequest = 0;
    QTimer pollTimer;
    QTimer stateTimer;
    State reportedState = State::Empty;
    quint64 generation = 0;
    quint64 preloadGeneration = 0;
};

}  // namespace Core
