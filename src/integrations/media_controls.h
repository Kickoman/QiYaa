#pragma once

#include <QObject>
#include <QUrl>

#include <functional>

namespace Core {
class CoverCache;
class Player;
class SourceRegistry;
}  // namespace Core

namespace Integrations {

class MediaControls : public QObject {
    Q_OBJECT
public:
    struct Hooks {
        std::function<int()> volume;
        std::function<void(int)> setVolume;
        std::function<void()> raise;
        std::function<void()> quit;
    };

    MediaControls(
        Core::Player* player,
        const Core::SourceRegistry* sources,
        Core::CoverCache* covers,
        Hooks hooks,
        QObject* parent = nullptr
    );

    Core::Player* player() const { return corePlayer; }
    const Hooks& hooks() const { return hookFunctions; }

    void play();
    void pause();
    void playPause();
    void stop();
    void next();
    void previous();
    bool seekTo(double seconds);
    bool canSeek() const;

    enum class Status { Playing, Paused, Stopped };
    Status status() const;
    QUrl artUrl() const;
    QUrl remoteArtUrl() const;
    QUrl webPage() const;

Q_SIGNALS:
    void trackChanged();
    void statusChanged();
    void artChanged();
    void modesChanged();
    void seeked(double seconds);
    void volumeChanged();

private:
    QUrl coverUrl() const;

    Core::Player* corePlayer;
    const Core::SourceRegistry* trackSources;
    Core::CoverCache* coverCache;
    Hooks hookFunctions;
};

}  // namespace Integrations
