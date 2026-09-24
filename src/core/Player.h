// Playback controller for the prototype: liked tracks -> playlist -> engine.
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QRandomGenerator>

#include "audio/AudioEngine.h"
#include "yandex/ApiClient.h"

class QNetworkReply;

namespace qiyaa {

class Player : public QObject {
    Q_OBJECT
public:
    Player(yandex::ApiClient* api, audio::AudioEngine* engine, QObject* parent = nullptr);

    // Reads the token, loads the account and liked tracks.
    void start();

    void play();
    void pause();
    void stop();
    void next();
    void previous();
    void seekFraction(double fraction);  // 0..1
    void setShuffle(bool on) { m_shuffle = on; }
    void setRepeat(bool on) { m_repeat = on; }
    bool shuffle() const { return m_shuffle; }
    bool repeat() const { return m_repeat; }

    const QList<yandex::Track>& playlist() const { return m_playlist; }
    int currentIndex() const { return m_index; }
    const yandex::Track* currentTrack() const;
    int currentBitrate() const { return m_bitrate; }
    double durationSeconds() const;
    audio::AudioEngine* engine() const { return m_engine; }

Q_SIGNALS:
    void statusMessage(const QString& text);
    void playlistLoaded();
    void currentTrackChanged();

private:
    void playIndex(int index);
    void startDownload(const QUrl& url, quint64 generation);
    void abortDownload();

    yandex::ApiClient* m_api;
    audio::AudioEngine* m_engine;
    yandex::Account m_account;
    QList<yandex::Track> m_playlist;
    int m_index = -1;
    int m_bitrate = 0;
    bool m_shuffle = false;
    bool m_repeat = false;
    quint64 m_generation = 0;  // invalidates callbacks of tracks we already skipped
    QPointer<QNetworkReply> m_download;
    QString m_playId;
};

}  // namespace qiyaa
