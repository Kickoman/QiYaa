// What the OS media integrations (MPRIS on Linux, SMTC on Windows) need from
// the app: commands to run and state to publish. Keeps them independent of
// the windows.
#pragma once

#include <functional>

#include <QObject>
#include <QString>
#include <QUrl>

namespace qiyaa {

class CoverCache;
class Player;

class MediaControls : public QObject {
    Q_OBJECT
public:
    struct Hooks {
        std::function<int()> volume;           // 0..100
        std::function<void(int)> setVolume;
        std::function<void()> raise;           // bring the windows to front
        std::function<void()> quit;
    };

    MediaControls(Player* player, CoverCache* covers, Hooks hooks, QObject* parent = nullptr);

    Player* player() const { return m_player; }
    const Hooks& hooks() const { return m_hooks; }

    // Commands with the exact semantics media keys expect (Player::pause toggles).
    void play();
    void pause();
    void playPause();
    void stop();
    void next();
    void previous();
    bool seekTo(double seconds);  // false if the track can't seek (yet)
    bool canSeek() const;

    enum class Status { Playing, Paused, Stopped };
    Status status() const;
    // Cover as a local file:// URL if cached, else the https URL (or empty).
    QUrl artUrl() const;
    // Always the https URL (or empty).
    QUrl remoteArtUrl() const;

Q_SIGNALS:
    void trackChanged();
    void statusChanged();
    void artChanged();
    void modesChanged();           // shuffle / repeat
    void seeked(double seconds);
    // The app emits this when its volume changes (the hooks only read/write it).
    void volumeChanged();

private:
    Player* m_player;
    CoverCache* m_covers;
    Hooks m_hooks;
};

}  // namespace qiyaa
