// Composition root: owns the audio engine, the Yandex client, the player and
// the three Winamp windows; handles layout, menus, shortcuts, settings, login.
#pragma once

#include "audio/audio_engine.h"
#include "core/player.h"
#include "skins/skin.h"
#include "yandex/api_client.h"
#include "yandex/library.h"

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
        QString skinOverride;  // --skin
        QString settingsFile;  // default: <configDir>/settings.ini
        bool offline = false;  // --offline: don't talk to Yandex
        bool audio = true;  // false for screenshots/tests
        bool readOnlySettings = false;  // use a throwaway settings file
        bool mediaIntegration = true;  // MPRIS / SMTC (off in tests and screenshots)
    };

    explicit Application(const Options& options, QObject* parent = nullptr);
    ~Application() override;

    // Shows the windows and connects to Yandex Music (or asks to log in).
    void start();

    Ui::MainWindow* mainWindow() const { return mainWindowInstance.get(); }
    Ui::EqualizerWindow* equalizerWindow() const { return equalizerWindowInstance.get(); }
    Ui::PlaylistWindow* playlistWindow() const { return playlistWindowInstance.get(); }
    Ui::NowPlayingWindow* nowPlayingWindow() const { return nowPlayingWindowInstance.get(); }
    Ui::MilkdropWindow* milkdropWindow() const {
        return milkdropWindowInstance.get();
    }  // null if built without Milkdrop
    Core::CoverCache* covers() const { return coverCache.get(); }
    Core::Player* player() { return &corePlayer; }
    Audio::AudioEngine* engine() { return &audioEngine; }
    Yandex::ApiClient* api() { return &apiClient; }
    Yandex::Library* library() { return &yandexLibrary; }

    bool loadSkin(const QString& path);
    // `persist` = remember it (false for the --scale command-line override).
    void setScale(double scale, bool persist = true);
    void setAlwaysOnTop(bool on);
    void setEqualizerVisible(bool on);
    void setPlaylistVisible(bool on);
    void setNowPlayingVisible(bool on);
    void setMilkdropVisible(bool on);

    void login();
    void logout();
    // Uses `token` for the API; on success optionally saves it and loads likes.
    void applyToken(const QString& token, bool save);

    void saveState();
    // Stops playback (so the wave hears about the track in progress), gives
    // the last reports a moment to leave, then quits.
    void quit();
    // Renders all visible windows, positioned as on screen, into one image.
    QImage snapshot() const;

private:
    void layoutWindows();
    void installShortcuts(QWidget* widget);
    void showMainMenu(QPoint globalPos);
    void showSourcesMenu(QPoint globalPos);
    void fillWindowActions(QMenu* menu);
    void transportKey(int key);  // Winamp's Z X C V B and the arrows
    QList<Ui::SkinnedWindow*> windows() const;

    Options startOptions;
    std::unique_ptr<QTemporaryDir> tmpDir;
    QSettings settings;
    Skins::Skin baseSkin;
    std::unique_ptr<Skins::Skin> currentSkin;
    bool transientScale = false;
    bool quitting = false;  // --scale: don't save positions made at this scale

    QNetworkAccessManager networkManager;
    Yandex::ApiClient apiClient;
    Yandex::Library yandexLibrary;
    Audio::AudioEngine audioEngine;
    Core::Player corePlayer;
    std::unique_ptr<Core::CoverCache> coverCache;
    std::unique_ptr<Integrations::MediaControls> mediaControls;
    std::unique_ptr<QObject> systemMediaControls;  // Mpris or Smtc

    // Declared last: destroyed first.
    std::unique_ptr<Ui::MainWindow> mainWindowInstance;
    std::unique_ptr<Ui::EqualizerWindow> equalizerWindowInstance;
    std::unique_ptr<Ui::PlaylistWindow> playlistWindowInstance;
    std::unique_ptr<Ui::NowPlayingWindow> nowPlayingWindowInstance;
    std::unique_ptr<Ui::MilkdropWindow> milkdropWindowInstance;
};

}  // namespace App
