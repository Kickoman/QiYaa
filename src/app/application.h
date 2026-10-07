#pragma once

#include "app/translations.h"
#include "audio/audio_engine.h"
#include "core/jam_mode.h"
#include "core/player.h"
#include "core/sources.h"
#include "skins/skin.h"
#include "yandex/api_client.h"
#include "yandex/library.h"

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>

#include <memory>
#include <optional>

class QMenu;
class QMessageBox;

namespace Core {
class CoverCache;
}  // namespace Core

namespace Jam {
class HostSession;
}  // namespace Jam

namespace Integrations {
class MediaControls;
}  // namespace Integrations

namespace Ui {
class EqualizerWindow;
class MainWindow;
class JamWindow;
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
        // Tests: a manager that reaches their mock server. Null: the app's own.
        QNetworkAccessManager* network = nullptr;
        // The interface language for this run, not saved. Unset: the `language` setting.
        std::optional<Language> language;
    };

    explicit Application(const Options& options, QObject* parent = nullptr);
    ~Application() override;

    void start();

    Ui::MainWindow* mainWindow() const { return mainWindowInstance.get(); }
    Ui::EqualizerWindow* equalizerWindow() const { return equalizerWindowInstance.get(); }
    Ui::PlaylistWindow* playlistWindow() const { return playlistWindowInstance.get(); }
    Ui::NowPlayingWindow* nowPlayingWindow() const { return nowPlayingWindowInstance.get(); }
    Ui::MilkdropWindow* milkdropWindow() const { return milkdropWindowInstance.get(); }
    Ui::JamWindow* jamWindow() const { return jamWindowInstance.get(); }  // null without the jam
    Core::CoverCache* covers() const { return coverCache.get(); }
    Core::Player* player() { return &corePlayer; }
    Audio::AudioEngine* engine() { return &audioEngine; }
    Yandex::ApiClient* api() { return &apiClient; }
    Yandex::Library* library() { return &yandexLibrary; }
    Core::JamMode* jam() { return &jamMode; }
    // Null when the app is built without the jam.
    Jam::HostSession* jamHost() const { return jamHostSession.get(); }

    bool loadSkin(const QString& path);
    enum class ScaleScope { Saved, ThisRun };
    void setScale(double scale, ScaleScope scope = ScaleScope::Saved);
    void setAlwaysOnTop(bool on);
    void setEqualizerVisible(bool on);
    void setPlaylistVisible(bool on);
    void setNowPlayingVisible(bool on);
    void setMilkdropVisible(bool on);
    void setJamWindowVisible(bool on);

    Language language() const { return translations.language(); }
    void setLanguage(Language language);

    void login();
    void logout();
    void applyToken(const QString& token, bool save);

    void saveState();
    void quit();
    QImage snapshot() const;

private:
    void layoutWindows();
    void installShortcuts(QWidget* widget);
    void showMainMenu(QPoint globalPosition, const Yandex::Track* track);
    void showSourcesMenu(QPoint globalPosition);
    void fillWindowActions(QMenu* menu);
    void transportKey(int key);
    QList<Ui::SkinnedWindow*> windows() const;
    void watchNetwork();
    void raiseWindows();
    void setUpJam();
    void offerStoredJam();
    void addJamMenu(QMenu* menu);
    void showJamServerDialog();

    Options startOptions;
    std::unique_ptr<QTemporaryDir> temporaryDirectory;
    QSettings settings;
    Translations translations;
    Skins::Skin baseSkin;
    std::unique_ptr<Skins::Skin> currentSkin;
    bool transientScale = false;
    bool quitting = false;

    std::unique_ptr<QNetworkAccessManager> ownNetworkManager;
    QNetworkAccessManager* networkManager;
    Yandex::ApiClient apiClient;
    Yandex::Library yandexLibrary;
    Audio::AudioEngine audioEngine;
    Core::Player corePlayer;
    Core::Sources sources;
    Core::JamMode jamMode;
    std::unique_ptr<Jam::HostSession> jamHostSession;
    QPointer<QMessageBox> storedJamQuestion;
    std::unique_ptr<Core::CoverCache> coverCache;
    std::unique_ptr<Integrations::MediaControls> mediaControls;
    std::unique_ptr<QObject> systemMediaControls;

    // Declared last: destroyed first.
    std::unique_ptr<Ui::MainWindow> mainWindowInstance;
    std::unique_ptr<Ui::EqualizerWindow> equalizerWindowInstance;
    std::unique_ptr<Ui::PlaylistWindow> playlistWindowInstance;
    std::unique_ptr<Ui::NowPlayingWindow> nowPlayingWindowInstance;
    std::unique_ptr<Ui::MilkdropWindow> milkdropWindowInstance;
    std::unique_ptr<Ui::JamWindow> jamWindowInstance;
};

}  // namespace App
