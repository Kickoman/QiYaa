// What the OS media integrations (MPRIS on Linux, SMTC on Windows) need from
// the app: commands to run and state to publish. Keeps them independent of
// the windows.
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

namespace Core {
class CoverCache;
class Player;
}  // namespace Core

namespace Integrations {

class MediaControls : public QObject {
    Q_OBJECT
public:
    struct Hooks {
        std::function<int()> volume;  // 0..100
        std::function<void(int)> setVolume;
        std::function<void()> raise;  // bring the windows to front
        std::function<void()> quit;
    };

    MediaControls(
        Core::Player* player,
        Core::CoverCache* covers,
        Hooks hooks,
        QObject* parent = nullptr
    );

    Core::Player* player() const { return corePlayer; }
    const Hooks& hooks() const { return hookFunctions; }

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
    void modesChanged();  // shuffle / repeat
    void seeked(double seconds);
    // The app emits this when its volume changes (the hooks only read/write it).
    void volumeChanged();

private:
    Core::Player* corePlayer;
    Core::CoverCache* coverCache;
    Hooks hookFunctions;
};

}  // namespace Integrations
