#pragma once

#include "audio/audio_engine.h"
#include "core/failure_policy.h"
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
};

struct PlayerNotice {
    NoticeCode code = NoticeCode::AudioError;
    QString detail;
    int count = 0;
    bool undecodable = false;
};

// How the queue's source wants its tracks treated; a new source (setQueue) has the defaults.
struct QueueRules {
    bool playReports = true;
    bool previousRestartsOnly = false;
};

class Player : public QObject {
    Q_OBJECT
public:
    using TLoadMoreCallback = std::function<void(std::function<void(const QList<Track>&)> done)>;
    using TrackEvent = Core::TrackEvent;
    using TEventCallback = std::function<void(TrackEvent event, const Track& track, double playedSeconds)>;
    enum class State { Empty, Stopped, Loading, Playing, Paused, WaitingForNetwork, WaitingForMore };
    Q_ENUM(State)

    Player(Audio::AudioEngine* engine, SourceRegistry* sources, QObject* parent = nullptr);

    quint64 newSourceRequest() { return ++sourceRequest; }
    bool isLatestSourceRequest(quint64 ticket) const { return ticket == sourceRequest; }

    void setQueue(
        const QList<Track>& tracks,
        const QString& title,
        bool autoplay,
        TLoadMoreCallback more = {},
        TEventCallback events = {}
    );
    void appendTracks(const QList<Track>& tracks);
    void insertTracks(int index, const QList<Track>& tracks);
    void removeTracks(QList<int> indices);
    void clearQueue();
    // Keeps the queue and the current track playing; replaces the source behind them.
    void changeSource(const QString& title, TLoadMoreCallback more, TEventCallback events, QueueRules rules);
    // The load-more check now, not only when a track becomes current.
    void requestMore() { maybeLoadMore(); }

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
    ShuffleAlgorithm shuffleAlgorithm() const { return preferredShuffleAlgorithm; }
    void setRepeat(bool on);
    bool shuffle() const { return shuffleEnabled; }  // the user's choice
    // Shuffle as it applies now: never in an endless queue (a wave plays in queue order).
    bool shuffleActive() const { return shuffleEnabled && !loadMore; }
    bool repeat() const { return repeatEnabled; }
    const QueueRules& rules() const { return queueRules; }
    State state() const;
    double positionSeconds() const;
    QUrl currentLink() const;
    QUrl nextLink() const;

    const QList<Track>& playlist() const { return queuedTracks; }
    const QString& queueTitle() const { return titleText; }
    int currentIndex() const { return playingIndex; }
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
    void statusMessage(const QString& text);
    void noticed(const Core::PlayerNotice& notice);
    void failed(Core::FailureKind kind);
    void stateChanged(Core::Player::State state);
    void playlistChanged();
    void queueReplaced();
    void currentTrackChanged();
    void positionTick();
    void modesChanged();
    void seeked(double seconds);

private:
    using TStreamId = Audio::AudioEngine::TStreamId;
    enum class DownloadState { Opening, Running, Finished };
    struct Feed {
        TStreamId stream = 0;
        std::unique_ptr<TrackDownload> download;
        DownloadState state = DownloadState::Opening;
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
        TEventCallback events;
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
    int pickNext() const;
    void resetNavigator();
    void maybePreload();
    void cancelPreload();
    void refreshPreload();
    void maybeLoadMore();
    void setWaitingForMore(bool waiting);
    double accumulatePlayedSeconds();
    void noteState();
    void reportState();

    Audio::AudioEngine* audioEngine;
    SourceRegistry* trackSources;
    QList<Track> queuedTracks;
    std::unique_ptr<TrackNavigator> navigator = MakeTrackNavigator(std::nullopt);
    ShuffleAlgorithm preferredShuffleAlgorithm = ShuffleAlgorithm::WithoutRepeats;
    QString titleText;
    TLoadMoreCallback loadMore;
    TEventCallback reportEvent;
    QueueRules queueRules;
    std::optional<CurrentTrack> current;
    std::optional<Preload> preload;
    int failuresInRow = 0;
    std::optional<bool> networkOnline;
    QTimer retryTimer;
    int retryFirstMs = 2'000;
    int retryMaxMs = 60'000;
    bool isShutDown = false;
    bool loadingMore = false;
    bool waitingForMore = false;
    quint64 sourceRequest = 0;
    QTimer pollTimer;
    QTimer stateTimer;
    State reportedState = State::Empty;
    quint64 queueGeneration = 0;
    quint64 generation = 0;
    quint64 preloadGeneration = 0;
    int playingIndex = -1;
    bool shuffleEnabled = false;
    bool repeatEnabled = false;
};

}  // namespace Core
