// Playback controller: the current queue (playlist) and what plays from it.
#pragma once

#include <functional>

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

    Player(yandex::Library* library, audio::AudioEngine* engine, QObject* parent = nullptr);

    // Loading a source is asynchronous; only the latest request may replace the
    // queue. Take a ticket before the request, check it in the callback.
    quint64 newSourceRequest() { return ++m_sourceRequest; }
    bool isLatestSourceRequest(quint64 ticket) const { return ticket == m_sourceRequest; }

    // Replaces the queue. `autoplay` starts the first track right away.
    void setQueue(const QList<yandex::Track>& tracks, const QString& title, bool autoplay, MoreFn more = {});
    void appendTracks(const QList<yandex::Track>& tracks);
    void removeTracks(QList<int> indices);
    void clearQueue();

    void play();
    void pause();
    void stop();
    void next();
    void previous();
    void playIndex(int index);
    void seekFraction(double fraction);  // 0..1
    void setShuffle(bool on) { m_shuffle = on; }
    void setRepeat(bool on) { m_repeat = on; }
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

Q_SIGNALS:
    void statusMessage(const QString& text);
    void playlistChanged();   // any change: append, remove, replace
    void queueReplaced();     // a new source replaced the whole queue
    void currentTrackChanged();
    void positionTick();      // ~10 Hz while something is loaded (for time displays)

private:
    void startDownload(const QUrl& url, quint64 generation);
    void abortDownload();
    void maybeLoadMore();

    yandex::Library* m_library;
    audio::AudioEngine* m_engine;
    QList<yandex::Track> m_playlist;
    QString m_title;
    MoreFn m_more;
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
};

}  // namespace qiyaa
