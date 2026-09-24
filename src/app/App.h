// Composition root: owns the audio engine, the Yandex client, the player and
// the three Winamp windows; handles layout, menus, shortcuts, settings, login.
#pragma once

#include <memory>

#include <QNetworkAccessManager>
#include <QObject>
#include <QSettings>
#include <QTemporaryDir>

#include "audio/AudioEngine.h"
#include "core/Player.h"
#include "skin/Skin.h"
#include "yandex/ApiClient.h"
#include "yandex/Library.h"

class QMenu;

namespace qiyaa {

class CoverCache;
class MediaControls;
class EqualizerWindow;
class NowPlayingWindow;
class MainWindow;
class MilkdropWindow;
class PlaylistWindow;
class SkinnedWindow;

class App : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString skinOverride;   // --skin
        QString settingsFile;   // default: <configDir>/settings.ini
        bool offline = false;   // --offline: don't talk to Yandex
        bool audio = true;      // false for screenshots/tests
        bool readOnlySettings = false;  // use a throwaway settings file
        bool mediaIntegration = true;   // MPRIS / SMTC (off in tests and screenshots)
    };

    explicit App(const Options& options, QObject* parent = nullptr);
    ~App() override;

    // Shows the windows and connects to Yandex Music (or asks to log in).
    void start();

    MainWindow* mainWindow() const { return m_main.get(); }
    EqualizerWindow* equalizerWindow() const { return m_eq.get(); }
    PlaylistWindow* playlistWindow() const { return m_pl.get(); }
    NowPlayingWindow* nowPlayingWindow() const { return m_np.get(); }
    MilkdropWindow* milkdropWindow() const { return m_md.get(); }  // null if built without Milkdrop
    CoverCache* covers() const { return m_covers.get(); }
    Player* player() { return &m_player; }
    audio::AudioEngine* engine() { return &m_engine; }
    yandex::ApiClient* api() { return &m_api; }
    yandex::Library* library() { return &m_library; }

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
    void installShortcuts(QWidget* w);
    void showMainMenu(QPoint globalPos);
    void showSourcesMenu(QPoint globalPos);
    void fillWindowActions(QMenu* menu);
    void transportKey(int key);  // Winamp's Z X C V B and the arrows
    QList<SkinnedWindow*> windows() const;

    Options m_options;
    std::unique_ptr<QTemporaryDir> m_tmpDir;
    QSettings m_settings;
    Skin m_baseSkin;
    std::unique_ptr<Skin> m_skin;
    bool m_transientScale = false;
    bool m_quitting = false;  // --scale: don't save positions made at this scale

    QNetworkAccessManager m_nam;
    yandex::ApiClient m_api;
    yandex::Library m_library;
    audio::AudioEngine m_engine;
    Player m_player;
    std::unique_ptr<CoverCache> m_covers;
    std::unique_ptr<MediaControls> m_mediaControls;
    std::unique_ptr<QObject> m_osMedia;  // Mpris or Smtc

    // Declared last: destroyed first.
    std::unique_ptr<MainWindow> m_main;
    std::unique_ptr<EqualizerWindow> m_eq;
    std::unique_ptr<PlaylistWindow> m_pl;
    std::unique_ptr<NowPlayingWindow> m_np;
    std::unique_ptr<MilkdropWindow> m_md;
};

}  // namespace qiyaa
