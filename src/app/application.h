#pragma once

#include "app/network_watch.h"
#include "app/settings.h"
#include "app/translations.h"
#include "audio/audio_engine.h"
#include "core/jam_mode.h"
#include "core/lyrics_finder.h"
#include "core/player.h"
#include "core/track.h"
#include "core/track_source.h"
#include "lrclib/service.h"
#include "skins/skin.h"
#include "sources/local/catalog.h"
#include "sources/local/source.h"
#include "sources/spotify/catalog.h"
#include "sources/spotify/library.h"
#include "sources/spotify/source.h"
#include "sources/yandex/catalog.h"
#include "sources/yandex/source.h"
#include "spotify/client/endpoints.h"
#include "spotify/client/session.h"
#include "spotify/client/web.h"
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
class LyricsWindow;
class MilkdropWindow;
class NowPlayingWindow;
class PlaylistWindow;
class SearchWindow;
class SkinnedWindow;
}  // namespace Ui

namespace App {

class Account;
class JamBridge;
class MainMenu;
class SpotifyAccount;
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
        SpotifyClient::Endpoints spotifyEndpoints;
    };

    explicit Application(const Options& options, QObject* parent = nullptr);
    ~Application() override;

    void start();

    Ui::MainWindow* mainWindow() const { return mainWindowInstance.get(); }
    Ui::EqualizerWindow* equalizerWindow() const { return equalizerWindowInstance.get(); }
    Ui::PlaylistWindow* playlistWindow() const { return playlistWindowInstance.get(); }
    Ui::NowPlayingWindow* nowPlayingWindow() const { return nowPlayingWindowInstance.get(); }
    Ui::MilkdropWindow* milkdropWindow() const { return milkdropWindowInstance.get(); }
    Ui::LyricsWindow* lyricsWindow() const { return lyricsWindowInstance.get(); }
    Ui::JamWindow* jamWindow() const { return jamWindowInstance.get(); }  // null without the jam
    Ui::SearchWindow* searchWindow() const { return searchWindowInstance.get(); }
    Core::CoverCache* covers() const { return coverCache.get(); }
    Core::Player* player() { return &corePlayer; }
    Audio::AudioEngine* engine() { return &audioEngine; }
    Yandex::ApiClient* api() { return &apiClient; }
    Yandex::Library* library() { return &yandexLibrary; }
    YandexSource::Source* yandex() { return &yandexSource; }
    Core::SourceRegistry* sources() { return &trackSources; }
    Core::JamMode* jam() { return &jamMode; }
    Core::LyricsFinder* lyrics() { return &lyricsFinder; }
    // Null when the app is built without the jam.
    Jam::HostSession* jamHost() const { return jamHostSession.get(); }

    WindowSet* windows() const { return windowSet.get(); }
    Account* account() const { return accountControl.get(); }
    SpotifyAccount* spotify() const { return spotifyAccountControl.get(); }
    SpotifyClient::Session* spotifySession() { return &spotifyClientSession; }
    SpotifyClient::Web* spotifyApi() { return &spotifyWeb; }
    SpotifySource::Source* spotifyTracks() { return &spotifySource; }
    SpotifySource::Catalog* spotifyPicks() { return &spotifyCatalog; }

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
    QString spotifyDeviceId;
    SpotifyClient::Session spotifyClientSession;
    SpotifyClient::Web spotifyWeb;
    SpotifySource::Library spotifyLibrary;
    Audio::AudioEngine audioEngine;
    YandexSource::Source yandexSource;
    LocalSource::Source localSource;
    SpotifySource::Source spotifySource;
    Core::SourceRegistry trackSources;
    Core::Player corePlayer;
    YandexSource::Catalog catalog;
    LocalSource::Catalog localCatalog;
    SpotifySource::Catalog spotifyCatalog;
    Core::JamMode jamMode;
    std::unique_ptr<Lrclib::Service> lrclibService;
    Core::LyricsFinder lyricsFinder;
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
    std::unique_ptr<Ui::LyricsWindow> lyricsWindowInstance;
    std::unique_ptr<Ui::SearchWindow> searchWindowInstance;
    std::unique_ptr<Ui::MilkdropWindow> milkdropWindowInstance;
    std::unique_ptr<Ui::JamWindow> jamWindowInstance;
    std::unique_ptr<WindowSet> windowSet;
    std::unique_ptr<Account> accountControl;
    std::unique_ptr<SpotifyAccount> spotifyAccountControl;
    std::unique_ptr<JamBridge> jamBridge;
    std::unique_ptr<TelemetryBridge> telemetryBridge;
    std::unique_ptr<MainMenu> mainMenu;
};

}  // namespace App
