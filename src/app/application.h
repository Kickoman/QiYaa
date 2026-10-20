#pragma once

#include "app/network_watch.h"
#include "app/settings.h"
#include "app/translations.h"
#include "audio/audio_engine.h"
#include "core/jam_mode.h"
#include "core/player.h"
#include "core/track.h"
#include "core/track_source.h"
#include "skins/skin.h"
#include "sources/yandex/catalog.h"
#include "sources/yandex/source.h"
#include "yandex/api_client.h"
#include "yandex/library.h"

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>
#include <optional>

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

class Account;
class JamBridge;
class MainMenu;
class TelemetryBridge;
class WindowSet;

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
        // Where telemetry goes; empty: none (tests, screenshots, builds without telemetry).
        QUrl telemetryUrl;
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
    YandexSource::Source* yandex() { return &yandexSource; }
    Core::SourceRegistry* sources() { return &trackSources; }
    Core::JamMode* jam() { return &jamMode; }
    // Null when the app is built without the jam.
    Jam::HostSession* jamHost() const { return jamHostSession.get(); }

    WindowSet* windows() const { return windowSet.get(); }
    Account* account() const { return accountControl.get(); }

    bool loadSkin(const QString& path);

    Language language() const { return translations.language(); }
    void setLanguage(Language language);

    TelemetryBridge* telemetry() const { return telemetryBridge.get(); }

    void saveState();
    void quit();

Q_SIGNALS:
    void languageChanged(App::Language language);
    void skinChanged(const QString& path);
    void skinFailed();

private:
    Options startOptions;
    std::unique_ptr<QTemporaryDir> temporaryDirectory;
    Settings settings;
    Translations translations;
    Skins::Skin baseSkin;
    std::unique_ptr<Skins::Skin> currentSkin;
    bool quitting = false;
    bool audioFailed = false;

    std::unique_ptr<QNetworkAccessManager> ownNetworkManager;
    QNetworkAccessManager* networkManager;
    Yandex::ApiClient apiClient;
    Yandex::Library yandexLibrary;
    Audio::AudioEngine audioEngine;
    YandexSource::Source yandexSource;
    Core::SourceRegistry trackSources;
    Core::Player corePlayer;
    YandexSource::Catalog catalog;
    Core::JamMode jamMode;
    std::unique_ptr<Jam::HostSession> jamHostSession;
    std::unique_ptr<Core::CoverCache> coverCache;
    std::unique_ptr<Integrations::MediaControls> mediaControls;
    std::unique_ptr<QObject> systemMediaControls;
    NetworkWatch networkWatch;

    // Declared last: destroyed first.
    std::unique_ptr<Ui::MainWindow> mainWindowInstance;
    std::unique_ptr<Ui::EqualizerWindow> equalizerWindowInstance;
    std::unique_ptr<Ui::PlaylistWindow> playlistWindowInstance;
    std::unique_ptr<Ui::NowPlayingWindow> nowPlayingWindowInstance;
    std::unique_ptr<Ui::MilkdropWindow> milkdropWindowInstance;
    std::unique_ptr<Ui::JamWindow> jamWindowInstance;
    std::unique_ptr<WindowSet> windowSet;
    std::unique_ptr<Account> accountControl;
    std::unique_ptr<JamBridge> jamBridge;
    std::unique_ptr<TelemetryBridge> telemetryBridge;
    std::unique_ptr<MainMenu> mainMenu;
};

}  // namespace App
