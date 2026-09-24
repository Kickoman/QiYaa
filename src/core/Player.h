// Playback controller: the current queue (playlist) and what plays from it.
#pragma once

#include <functional>
#include <optional>

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include "audio/AudioEngine.h"
#include "yandex/Library.h"

class QNetworkReply;

namespace qiyaa {

class Player : public QObject {
    Q_OBJECT
public:
    // Asked to append more tracks when the queue is about to run out (endless waves).
    using MoreFn = std::function<void(std::function<void(const QList<yandex::Track>&)> done)>;
    // What happened to a track of the queue (used for wave feedback).
    enum class TrackEvent { Started, Finished, Skipped };
    using EventFn = std::function<void(TrackEvent event, const yandex::Track& track, double playedSeconds)>;

    Player(yandex::Library* library, audio::AudioEngine* engine, QObject* parent = nullptr);

    // Loading a source is asynchronous; only the latest request may replace the
    // queue. Take a ticket before the request, check it in the callback.
    quint64 newSourceRequest() { return ++m_sourceRequest; }
    bool isLatestSourceRequest(quint64 ticket) const { return ticket == m_sourceRequest; }

    // Replaces the queue. `autoplay` starts the first track right away.
    void setQueue(const QList<yandex::Track>& tracks, const QString& title, bool autoplay, MoreFn more = {},
                  EventFn events = {});
    void appendTracks(const QList<yandex::Track>& tracks);
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
    bool shuffle() const { return m_shuffle; }
    bool repeat() const { return m_repeat; }

    const QList<yandex::Track>& playlist() const { return m_playlist; }
    const QString& queueTitle() const { return m_title; }
    int currentIndex() const { return m_index; }
    const yandex::Track* currentTrack() const;
    int currentBitrate() const { return m_bitrate; }
    double durationSeconds() const;
    audio::AudioEngine* engine() const { return m_engine; }
    yandex::Library* library() const { return m_library; }
    // Index of the track already downloading in the background to follow the
    // current one without a gap (-1 if none yet).
    int preloadedIndex() const { return m_preload && m_preload->stream ? m_preload->index : -1; }

Q_SIGNALS:
    void statusMessage(const QString& text);
    void playlistChanged();   // any change: append, remove, replace
    void queueReplaced();     // a new source replaced the whole queue
    void currentTrackChanged();
    void positionTick();      // ~10 Hz while something is loaded (for time displays)
    void modesChanged();      // shuffle or repeat
    void seeked(double seconds);

private:
    using StreamId = audio::AudioEngine::StreamId;
    // The next track, resolved and downloading into a queued engine stream.
    struct Preload {
        int index = -1;
        QString trackId;
        quint64 gen = 0;           // invalidates its link request
        StreamId stream = 0;       // 0 until the link is resolved
        int bitrate = 0;
        QPointer<QNetworkReply> reply;
        bool downloadDone = false;
        bool failed = false;
    };

    QNetworkReply* startDownload(const QUrl& url, StreamId stream);
    void downloadFinished(StreamId stream, bool failed, const QString& error);
    void abortDownload();
    // Reports the start of `track` (it is current and its audio is on the way).
    void trackStarted(const yandex::Track& track, int bitrate);
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

    yandex::Library* m_library;
    audio::AudioEngine* m_engine;
    QList<yandex::Track> m_playlist;
    QString m_title;
    MoreFn m_more;
    EventFn m_events;
    // The track whose Started was reported and that hasn't finished/skipped yet.
    std::optional<yandex::Track> m_openTrack;
    EventFn m_openTrackEvents;
    double m_played = 0;
    double m_lastPosition = 0;
    bool m_downloadFailed = false;
    bool m_shutDown = false;
    bool m_loadingMore = false;
    bool m_waitingForMore = false;  // reached the end of an endless queue; play when more arrives
    quint64 m_sourceRequest = 0;
    QTimer m_pollTimer;
    quint64 m_queueGeneration = 0;
    int m_index = -1;
    int m_bitrate = 0;
    bool m_shuffle = false;
    bool m_repeat = false;
    quint64 m_generation = 0;  // invalidates callbacks of tracks we already skipped
    QPointer<QNetworkReply> m_download;
    StreamId m_stream = 0;               // the current track's engine stream
    bool m_currentDownloaded = false;    // the whole current track is in memory
    std::optional<Preload> m_preload;
    quint64 m_preloadGen = 0;
};

}  // namespace qiyaa
