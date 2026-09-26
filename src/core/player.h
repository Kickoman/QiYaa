// Playback controller: the current queue (playlist) and what plays from it.
#pragma once

#include "audio/audio_engine.h"
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

class Player : public QObject {
    Q_OBJECT
public:
    // Asked to append more tracks when the queue is about to run out (endless waves).
    using TMoreFn = std::function<void(std::function<void(const QList<Yandex::Track>&)> done)>;
    // What happened to a track of the queue (used for wave feedback).
    enum class TrackEvent { Started, Finished, Skipped };
    using TEventFn =
        std::function<void(TrackEvent event, const Yandex::Track& track, double playedSeconds)>;

    Player(Yandex::Library* library, Audio::AudioEngine* engine, QObject* parent = nullptr);

    // Loading a source is asynchronous; only the latest request may replace the
    // queue. Take a ticket before the request, check it in the callback.
    quint64 newSourceRequest() { return ++sourceRequest; }
    bool isLatestSourceRequest(quint64 ticket) const { return ticket == sourceRequest; }

    // Replaces the queue. `autoplay` starts the first track right away.
    void setQueue(
        const QList<Yandex::Track>& tracks,
        const QString& title,
        bool autoplay,
        TMoreFn more = {},
        TEventFn events = {}
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
    // For quitting: stops and ignores anything that would start playback again
    // (sources still loading, media keys).
    void shutDown();
    // Absolute seek in seconds (clamped to the track); false if it can't seek (yet).
    bool seekTo(double seconds);
    bool seekFraction(double fraction);  // 0..1; false if the track can't seek (yet)
    void setShuffle(bool on);
    void setRepeat(bool on);
    bool shuffle() const { return shuffleEnabled; }
    bool repeat() const { return repeatEnabled; }

    const QList<Yandex::Track>& playlist() const { return queuedTracks; }
    const QString& queueTitle() const { return titleText; }
    int currentIndex() const { return playingIndex; }
    const Yandex::Track* currentTrack() const;
    int currentBitrate() const { return bitrateKbps; }
    double durationSeconds() const;
    Audio::AudioEngine* engine() const { return audioEngine; }
    Yandex::Library* library() const { return yandexLibrary; }
    // Index of the track already downloading in the background to follow the
    // current one without a gap (-1 if none yet).
    int preloadedIndex() const { return preload && preload->stream ? preload->index : -1; }

Q_SIGNALS:
    void statusMessage(const QString& text);
    void playlistChanged();  // any change: append, remove, replace
    void queueReplaced();  // a new source replaced the whole queue
    void currentTrackChanged();
    void positionTick();  // ~10 Hz while something is loaded (for time displays)
    void modesChanged();  // shuffle or repeat
    void seeked(double seconds);

private:
    using TStreamId = Audio::AudioEngine::TStreamId;
    // The next track, resolved and downloading into a queued engine stream.
    struct Preload {
        int index = -1;
        QString trackId;
        quint64 gen = 0;  // invalidates its link request
        TStreamId stream = 0;  // 0 until the link is resolved
        int bitrate = 0;
        QPointer<QNetworkReply> reply;
        bool downloadDone = false;
        bool failed = false;
    };

    QNetworkReply* startDownload(const QUrl& url, TStreamId stream);
    void downloadFinished(TStreamId stream, bool failed, const QString& error);
    void abortDownload();
    // Reports the start of `track` (it is current and its audio is on the way).
    void trackStarted(const Yandex::Track& track, int bitrate);
    // What plays after the current track: in order (-1 at the end of a finite
    // queue, or of an endless one that is still loading), or at random.
    int sequentialNext() const;
    int pickNext() const;
    void maybePreload();
    void cancelPreload();
    // After queue or mode changes: keep the preload if it's still what follows.
    void refreshPreload();
    void maybeLoadMore();
    // Reports Skipped for the track that is playing (if its Started was sent).
    void closeOpenTrack();
    // Seconds of the open track actually heard (seeks don't count).
    double playedSeconds();

    Yandex::Library* yandexLibrary;
    Audio::AudioEngine* audioEngine;
    QList<Yandex::Track> queuedTracks;
    QString titleText;
    TMoreFn loadMore;
    TEventFn reportEvent;
    // The track whose Started was reported and that hasn't finished/skipped yet.
    std::optional<Yandex::Track> openTrack;
    TEventFn openTrackEvents;
    double played = 0;
    double lastPosition = 0;
    bool downloadFailed = false;
    bool isShutDown = false;
    bool loadingMore = false;
    bool waitingForMore = false;  // reached the end of an endless queue; play when more arrives
    quint64 sourceRequest = 0;
    QTimer pollTimer;
    quint64 queueGeneration = 0;
    int playingIndex = -1;
    int bitrateKbps = 0;
    bool shuffleEnabled = false;
    bool repeatEnabled = false;
    quint64 generation = 0;  // invalidates callbacks of tracks we already skipped
    QPointer<QNetworkReply> download;
    TStreamId streamId = 0;  // the current track's engine stream
    bool currentDownloaded = false;  // the whole current track is in memory
    std::optional<Preload> preload;
    quint64 preloadGen = 0;
};

}  // namespace Core
