#pragma once

#include "audio/audio_engine.h"
#include "core/failure_policy.h"
#include "yandex/api_client.h"
#include "yandex/library.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <functional>
#include <optional>

class QNetworkReply;

namespace Core {

// Previous goes back a track only near a track's start; later it restarts the track.
inline constexpr double kPreviousRestartsAfterSeconds = 3.0;

// What Previous does at `positionSeconds` into track `cursor` of `size`: the index to play, or -1
// to restart the current track from 0 (spec TR-01, TR-02).
int PreviousTarget(double positionSeconds, int cursor, int size, bool repeat);

class Player : public QObject {
    Q_OBJECT
public:
    using TLoadMoreCallback =
        std::function<void(std::function<void(const QList<Yandex::Track>&)> done)>;
    enum class TrackEvent { Started, Finished, Skipped };
    using TEventCallback =
        std::function<void(TrackEvent event, const Yandex::Track& track, double playedSeconds)>;

    Player(Yandex::Library* library, Audio::AudioEngine* engine, QObject* parent = nullptr);

    quint64 newSourceRequest() { return ++sourceRequest; }
    bool isLatestSourceRequest(quint64 ticket) const { return ticket == sourceRequest; }

    void setQueue(
        const QList<Yandex::Track>& tracks,
        const QString& title,
        bool autoplay,
        TLoadMoreCallback more = {},
        TEventCallback events = {}
    );
    void appendTracks(const QList<Yandex::Track>& tracks);
    void removeTracks(QList<int> indices);
    void clearQueue();

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
    void setRepeat(bool on);
    bool shuffle() const { return shuffleEnabled; }  // the user's choice
    // Shuffle as it applies now: never in an endless queue (a wave plays in queue order).
    bool shuffleActive() const { return shuffleEnabled && !loadMore; }
    bool repeat() const { return repeatEnabled; }

    const QList<Yandex::Track>& playlist() const { return queuedTracks; }
    const QString& queueTitle() const { return titleText; }
    int currentIndex() const { return playingIndex; }
    const Yandex::Track* currentTrack() const;
    int currentBitrate() const { return bitrateKbps; }
    double durationSeconds() const;
    Audio::AudioEngine* engine() const { return audioEngine; }
    Yandex::Library* library() const { return yandexLibrary; }
    int preloadedIndex() const;

    // What the system says about the network: nullopt when it cannot tell, and then only the
    // retry timer ends a wait for the network. Coming online retries a waiting track at once.
    void setNetworkOnline(std::optional<bool> online);
    // The delays between tries while waiting for the network: `firstMs`, doubling up to `maxMs`
    // (2 s and 60 s unless a test sets them).
    void setNetworkRetryDelays(int firstMs, int maxMs);
    bool isWaitingForNetwork() const { return networkWait.has_value(); }

Q_SIGNALS:
    void statusMessage(const QString& text);
    void playlistChanged();
    void queueReplaced();
    void currentTrackChanged();
    void positionTick();
    void modesChanged();
    void seeked(double seconds);

private:
    using TStreamId = Audio::AudioEngine::TStreamId;
    struct Preload {
        int index = -1;
        QString trackId;
        quint64 generation = 0;
        TStreamId stream = 0;
        int bitrate = 0;
        QPointer<QNetworkReply> reply;
        bool downloadDone = false;
    };
    // A track parked by a network failure (spec ERR-01 to ERR-03): its stream starts again at
    // `position`, and plays on if `resume`.
    struct NetworkWait {
        bool resume = true;
        double position = 0;
        int attempt = 0;
    };

    QNetworkReply* startDownload(const QUrl& url, TStreamId stream);
    void downloadFinished(TStreamId stream, const Yandex::RequestError& error);
    void handleFailure(FailureKind kind, const QString& text);
    void waitForNetwork();
    void retryAfterNetwork();
    void cancelNetworkWait();
    void abortDownload();
    void trackStarted(const Yandex::Track& track, int bitrate);
    int sequentialNext() const;
    int pickNext() const;
    void maybePreload();
    void cancelPreload();
    void refreshPreload();
    void maybeLoadMore();
    void closeOpenTrack();
    double accumulatePlayedSeconds();

    Yandex::Library* yandexLibrary;
    Audio::AudioEngine* audioEngine;
    QList<Yandex::Track> queuedTracks;
    QString titleText;
    TLoadMoreCallback loadMore;
    TEventCallback reportEvent;
    std::optional<Yandex::Track> openTrack;
    TEventCallback openTrackEvents;
    double playedSeconds = 0;
    double lastPosition = 0;
    std::optional<FailureKind> streamFailure;  // the current stream's download broke
    qint64 currentBytes = 0;  // audio bytes the current stream has received
    int failuresInRow = 0;
    std::optional<NetworkWait> networkWait;
    std::optional<bool> networkOnline;
    QTimer retryTimer;
    int retryFirstMs = 2'000;
    int retryMaxMs = 60'000;
    bool isShutDown = false;
    bool loadingMore = false;
    bool waitingForMore = false;
    quint64 sourceRequest = 0;
    QTimer pollTimer;
    quint64 queueGeneration = 0;
    int playingIndex = -1;
    int bitrateKbps = 0;
    bool shuffleEnabled = false;
    bool repeatEnabled = false;
    quint64 generation = 0;
    QPointer<QNetworkReply> download;
    TStreamId streamId = 0;
    bool currentDownloaded = false;
    std::optional<Preload> preload;
    quint64 preloadGeneration = 0;
};

}  // namespace Core
