#pragma once

#include "audio/audio_engine.h"
#include "core/player.h"
#include "skins/skin.h"
#include "yandex/api_client.h"
#include "yandex/library.h"

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSettings>
#include <QTemporaryDir>

#include <memory>

class QMenu;

namespace Core {
class CoverCache;
}  // namespace Core

namespace Integrations {
class MediaControls;
}  // namespace Integrations

namespace Ui {
class EqualizerWindow;
class MainWindow;
class MilkdropWindow;
class NowPlayingWindow;
class PlaylistWindow;
class SkinnedWindow;
}  // namespace Ui

namespace App {

class Application : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString skinOverride;
        QString settingsFile;
        bool offline = false;
        bool audio = true;
        bool readOnlySettings = false;
        bool mediaIntegration = true;
    };

    explicit Application(const Options& options, QObject* parent = nullptr);
    ~Application() override;

    void start();

    Ui::MainWindow* mainWindow() const { return mainWindowInstance.get(); }
    Ui::EqualizerWindow* equalizerWindow() const { return equalizerWindowInstance.get(); }
    Ui::PlaylistWindow* playlistWindow() const { return playlistWindowInstance.get(); }
    Ui::NowPlayingWindow* nowPlayingWindow() const { return nowPlayingWindowInstance.get(); }
    Ui::MilkdropWindow* milkdropWindow() const { return milkdropWindowInstance.get(); }
    Core::CoverCache* covers() const { return coverCache.get(); }
    Core::Player* player() { return &corePlayer; }
    Audio::AudioEngine* engine() { return &audioEngine; }
    Yandex::ApiClient* api() { return &apiClient; }
    Yandex::Library* library() { return &yandexLibrary; }

    bool loadSkin(const QString& path);
    enum class ScaleScope { Saved, ThisRun };
    void setScale(double scale, ScaleScope scope = ScaleScope::Saved);
    void setAlwaysOnTop(bool on);
    void setEqualizerVisible(bool on);
    void setPlaylistVisible(bool on);
    void setNowPlayingVisible(bool on);
    void setMilkdropVisible(bool on);

    void login();
    void logout();
    void applyToken(const QString& token, bool save);

    void saveState();
    void quit();
    QImage snapshot() const;

private:
    void layoutWindows();
    void installShortcuts(QWidget* widget);
    void showMainMenu(QPoint globalPosition);
    void showSourcesMenu(QPoint globalPosition);
    void fillWindowActions(QMenu* menu);
    void transportKey(int key);
    QList<Ui::SkinnedWindow*> windows() const;

    Options startOptions;
    std::unique_ptr<QTemporaryDir> temporaryDirectory;
    QSettings settings;
    Skins::Skin baseSkin;
    std::unique_ptr<Skins::Skin> currentSkin;
    bool transientScale = false;
    bool quitting = false;

    QNetworkAccessManager networkManager;
    Yandex::ApiClient apiClient;
    Yandex::Library yandexLibrary;
    Audio::AudioEngine audioEngine;
    Core::Player corePlayer;
    std::unique_ptr<Core::CoverCache> coverCache;
    std::unique_ptr<Integrations::MediaControls> mediaControls;
    std::unique_ptr<QObject> systemMediaControls;

    // Declared last: destroyed first.
    std::unique_ptr<Ui::MainWindow> mainWindowInstance;
    std::unique_ptr<Ui::EqualizerWindow> equalizerWindowInstance;
    std::unique_ptr<Ui::PlaylistWindow> playlistWindowInstance;
    std::unique_ptr<Ui::NowPlayingWindow> nowPlayingWindowInstance;
    std::unique_ptr<Ui::MilkdropWindow> milkdropWindowInstance;
};

}  // namespace App
