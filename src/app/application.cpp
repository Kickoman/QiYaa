#include "app/application.h"

#include "app/account.h"
#include "app/jam_bridge.h"
#include "app/main_menu.h"
#include "app/paths.h"
#include "app/spotify_account.h"
#include "app/telemetry_bridge.h"
#include "app/window_set.h"
#include "audio/equalizer.h"
#include "core/cover_cache.h"
#include "integrations/media_controls.h"
#include "jam/host_session.h"
#include "skins/error.h"
#include "sources/local/tracks.h"
#include "sources/spotify/tracks.h"
#include "sources/yandex/history_wave.h"
#include "sources/yandex/tracks.h"
#include "spotify/client/identity.h"
#include "ui/equalizer_window.h"
#include "ui/jam_window.h"
#include "ui/lyrics_window.h"
#include "ui/main_window.h"
#include "ui/milkdrop_window.h"
#include "ui/now_playing_window.h"
#include "ui/playlist_window.h"
#include "ui/search_window.h"
#include "ui/source_notices.h"
#ifdef QIYAA_HAVE_MPRIS
#include "integrations/mpris.h"
#endif
#ifdef QIYAA_HAVE_SMTC
#include "integrations/smtc.h"
#endif
#ifdef QIYAA_HAVE_MAC_MEDIA_CONTROLS
#include "integrations/mac_media_controls.h"
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QKeySequence>
#include <QString>
#include <QTimer>

#include <utility>

namespace App {

namespace {

QString SpotifyDeviceId(const Settings& settings) {
    const QString saved = settings.spotifyDeviceId();
    return saved.isEmpty() ? SpotifyClient::NewDeviceId() : saved;
}

}  // namespace

Application::Application(const Options& options, QObject* parent)
    : QObject(parent)
    , startOptions(options)
    , temporaryDirectory(options.readOnlySettings ? std::make_unique<QTemporaryDir>() : nullptr)
    , settings(SettingsFile(options.settingsFile, temporaryDirectory.get()))
    , baseSkin(Skins::Skin::BuiltinBase())
    , currentSkin(std::make_unique<Skins::Skin>(baseSkin))
    , ownNetworkManager(options.network ? nullptr : std::make_unique<QNetworkAccessManager>())
    , networkManager(options.network ? options.network : ownNetworkManager.get())
    , apiClient(networkManager)
    , yandexLibrary(&apiClient)
    , spotifyDeviceId(SpotifyDeviceId(settings))
    , spotifyClientSession(
          networkManager,
          options.spotifyEndpoints,
          SpotifyClient::Identity(QCoreApplication::applicationVersion(), spotifyDeviceId)
      )
    , spotifyWeb(
          networkManager,
          options.spotifyEndpoints,
          &spotifyClientSession,
          spotifyDeviceId,
          QCoreApplication::applicationVersion()
      )
    , spotifyLibrary(&spotifyWeb, &spotifyClientSession)
    , yandexSource(&yandexLibrary)
    , spotifySource(&spotifyWeb, &spotifyClientSession, networkManager)
    , corePlayer(&audioEngine, &trackSources)
    , catalog(&corePlayer, &yandexLibrary)
    , localCatalog(&corePlayer, LocalCoversDirectory(temporaryDirectory.get()))
    , spotifyCatalog(&corePlayer, &spotifyLibrary)
    , jamMode(&corePlayer)
    , lrclibService(
          options.offline
              ? nullptr
              : std::make_unique<
                    Lrclib::Service>(networkManager, Lrclib::kBaseUrl, QCoreApplication::applicationVersion())
      )
    , lyricsFinder(&corePlayer, &trackSources, lrclibService.get()) {
    trackSources.add(YandexSource::kSourceName, &yandexSource);
    trackSources.add(LocalSource::kSourceName, &localSource);
    trackSources.add(SpotifySource::kSourceName, &spotifySource);
    // Before any window: they take their texts from the translation.
    translations.apply(options.language.value_or(settings.language().value_or(kDefaultLanguage)));
    corePlayer.setShuffleAlgorithm(settings.shuffleAlgorithm());
    yandexSource.setQuality(settings.quality());
    spotifySource.setEconomy(settings.quality() == Yandex::Quality::Economy);
    corePlayer.setContinueWithVibeByDefault(settings.continueWithVibe());
    corePlayer.setHistoryWaveFactory(YandexSource::HistoryWaves(&yandexLibrary));
    lyricsFinder.setThirdPartyEnabled(settings.lyricsThirdParty());
    connect(&yandexLibrary, &Yandex::Library::accountChanged, &lyricsFinder, &Core::LyricsFinder::forget);
    connect(&networkWatch, &NetworkWatch::back, &lyricsFinder, &Core::LyricsFinder::retryFailed);
    connect(&corePlayer, &Core::Player::modesChanged, this, [this] {
        settings.setShuffleAlgorithm(corePlayer.shuffleAlgorithm());
    });
    if (startOptions.audio) {
        if (const Audio::AudioEngine::InitResult initResult = audioEngine.init(); !initResult.ok) {
            qWarning("Audio: %s", qPrintable(initResult.message));
            audioFailed = true;
        } else {
            qInfo("Audio backend: %s", qPrintable(audioEngine.backendName()));
        }
    }

    const QString skinPath = options.skinOverride.isEmpty() ? settings.skin() : options.skinOverride;
    if (!skinPath.isEmpty()) {
        try {
            currentSkin = std::make_unique<Skins::Skin>(Skins::Skin::LoadFile(skinPath, &baseSkin));
        } catch (const Skins::Error& error) {
            qWarning("Skin: %s; using the built-in one", error.what());
        }
    }

    mainWindowInstance = std::make_unique<Ui::MainWindow>(&corePlayer, &audioEngine, currentSkin.get());
    equalizerWindowInstance = std::make_unique<Ui::EqualizerWindow>(currentSkin.get());
    playlistWindowInstance = std::make_unique<Ui::PlaylistWindow>(&corePlayer, currentSkin.get());
    coverCache = std::make_unique<Core::CoverCache>(
        networkManager, temporaryDirectory ? temporaryDirectory->filePath(QStringLiteral("covers")) : QString()
    );
    nowPlayingWindowInstance = std::make_unique<Ui::NowPlayingWindow>(
        &corePlayer, &catalog, &trackSources, coverCache.get(), currentSkin.get()
    );
    windowSet = std::make_unique<WindowSet>(
        &settings, mainWindowInstance.get(), WindowSet::Spec{.group = QStringLiteral("mainWindow")}
    );
    windowSet->add(
        equalizerWindowInstance.get(),
        {.group = QStringLiteral("equalizer"),
         .place = WindowSet::Place::Stacked,
         .visibleByDefault = true,
         .keepsShade = true,
         .button = WindowSet::Button::Equalizer,
         .menuText = QT_TRANSLATE_NOOP("App::MainMenu", "Equalizer"),
         .key = QKeySequence(Qt::ALT | Qt::Key_G)}
    );
    windowSet->add(
        playlistWindowInstance.get(),
        {.group = QStringLiteral("playlist"),
         .place = WindowSet::Place::Stacked,
         .visibleByDefault = true,
         .keepsShade = true,
         .stepsByDefault = QSize(0, 4),
         .button = WindowSet::Button::Playlist,
         .menuText = QT_TRANSLATE_NOOP("App::MainMenu", "Playlist"),
         .key = QKeySequence(Qt::ALT | Qt::Key_E)}
    );
    windowSet->add(
        nowPlayingWindowInstance.get(),
        {.group = QStringLiteral("nowPlaying"),
         .stepsByDefault = QSize(0, 0),
         .menuText = QT_TRANSLATE_NOOP("App::MainMenu", "Now playing")}
    );
    lyricsWindowInstance = std::make_unique<Ui::LyricsWindow>(&lyricsFinder, &corePlayer, currentSkin.get());
    windowSet->add(
        lyricsWindowInstance.get(),
        {.group = QStringLiteral("lyrics"),
         .place = WindowSet::Place::Stacked,
         .stepsByDefault = QSize(0, 6),
         .menuText = QT_TRANSLATE_NOOP("App::MainMenu", "Lyrics"),
         .key = QKeySequence(Qt::ALT | Qt::Key_L)}
    );
    accountControl = std::make_unique<Account>(
        Account::Parts{&apiClient, &yandexLibrary, &corePlayer, &catalog, networkManager, mainWindowInstance.get()},
        Account::TokenFiles{TokenFile(), YaampTokenFiles(), ConfigDirectory()}
    );
    connect(accountControl.get(), &Account::statusText, mainWindowInstance.get(), &Ui::MainWindow::setStatusText);
    const QString spotifyCredentials = temporaryDirectory
        ? temporaryDirectory->filePath(QStringLiteral("spotify-credentials"))
        : SpotifyCredentialsFile();
    spotifyAccountControl = std::make_unique<SpotifyAccount>(
        SpotifyAccount::Parts{
            &spotifyClientSession, &spotifyWeb, networkManager, mainWindowInstance.get(), options.spotifyEndpoints
        },
        spotifyCredentials
    );
    connect(
        spotifyAccountControl.get(),
        &SpotifyAccount::statusText,
        mainWindowInstance.get(),
        &Ui::MainWindow::setStatusText
    );
    connect(spotifyAccountControl.get(), &SpotifyAccount::connected, this, [this] {
        if (settings.spotifyDeviceId().isEmpty()) {
            settings.setSpotifyDeviceId(spotifyDeviceId);
        }
        spotifyCatalog.setLoggedIn(true);
        searchWindowInstance->update();
    });
    const auto spotifyGone = [this] {
        spotifyCatalog.setLoggedIn(false);
        searchWindowInstance->update();
    };
    connect(spotifyAccountControl.get(), &SpotifyAccount::loggedOut, this, spotifyGone);
    connect(spotifyAccountControl.get(), &SpotifyAccount::refused, this, spotifyGone);
    spotifyCatalog.setLoggedIn(spotifyAccountControl->loggedIn());
    searchWindowInstance = std::make_unique<Ui::SearchWindow>(
        QList<Ui::SearchWindow::Tab>{
            {QT_TRANSLATE_NOOP("Ui::SearchWindow", "Yandex Music"), &catalog},
            {QT_TRANSLATE_NOOP("Ui::SearchWindow", "Spotify"), &spotifyCatalog},
        },
        &corePlayer,
        currentSkin.get()
    );
    windowSet->add(
        searchWindowInstance.get(),
        {.group = QStringLiteral("search"),
         .shownAtStart = false,
         .raisedWhenShown = true,
         .stepsByDefault = QSize(3, 12),
         .menuText = QT_TRANSLATE_NOOP("App::MainMenu", "Search"),
         .key = QKeySequence(Qt::CTRL | Qt::Key_F)}
    );
    connect(
        accountControl.get(), &Account::connected, searchWindowInstance.get(), qOverload<>(&Ui::SearchWindow::update)
    );
    connect(accountControl.get(), &Account::loggingOut, this, [this] {
        QTimer::singleShot(0, searchWindowInstance.get(), qOverload<>(&Ui::SearchWindow::update));
    });
    connect(searchWindowInstance.get(), &Ui::SearchWindow::loginRequested, this, [this](int tab) {
        if (tab == 0) {
            QTimer::singleShot(0, accountControl.get(), &Account::login);
        } else {
            QTimer::singleShot(0, spotifyAccountControl.get(), &SpotifyAccount::login);
        }
    });
#if defined(QIYAA_HAVE_MILKDROP)
    {
        const QString userPresets =
            (temporaryDirectory ? temporaryDirectory->path() : App::ConfigDirectory()) + QStringLiteral("/milkdrop");
        milkdropWindowInstance = std::make_unique<Ui::MilkdropWindow>(
            &audioEngine, QStringLiteral(":/milkdrop"), userPresets, currentSkin.get()
        );
        windowSet->add(
            milkdropWindowInstance.get(),
            {.group = QStringLiteral("milkdrop"),
             .place = WindowSet::Place::BelowRightOfMain,
             .stepsByDefault = QSize(0, 4),
             .menuText = QT_TRANSLATE_NOOP("App::MainMenu", "Milkdrop"),
             .key = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K)}
        );
        const MilkdropSettings milkdrop = settings.milkdrop();
        milkdropWindowInstance->setShuffle(milkdrop.shuffle);
        milkdropWindowInstance->setLocked(milkdrop.locked);
        milkdropWindowInstance->setPresetSeconds(milkdrop.seconds);
        milkdropWindowInstance->setBlackPresets(milkdrop.black);
        milkdropWindowInstance->selectPreset(milkdrop.preset);
        connect(milkdropWindowInstance.get(), &Ui::MilkdropWindow::settingsChanged, this, [this] {
            settings.setMilkdrop(MilkdropSettings{
                milkdropWindowInstance->shuffle(),
                milkdropWindowInstance->locked(),
                milkdropWindowInstance->presetSeconds(),
                milkdropWindowInstance->currentPreset(),
                milkdropWindowInstance->blackPresets()
            });
        });
        connect(
            milkdropWindowInstance.get(),
            &Ui::MilkdropWindow::statusText,
            mainWindowInstance.get(),
            &Ui::MainWindow::setStatusText
        );
        connect(milkdropWindowInstance.get(), &Ui::MilkdropWindow::transportKey, this, [this](int key) {
            TransportKey(&corePlayer, &audioEngine, key);
        });
        connect(
            &audioEngine,
            &Audio::AudioEngine::stateChanged,
            milkdropWindowInstance.get(),
            [this](Audio::AudioEngine::State state) {
                milkdropWindowInstance->setPlaying(state == Audio::AudioEngine::State::Playing);
            }
        );
    }
#endif

    connect(&catalog, &YandexSource::Catalog::noticed, this, [this](const Core::SourceNotice& notice) {
        mainWindowInstance->setStatusText(Ui::SourceNoticeText(notice));
    });
    connect(&localCatalog, &LocalSource::Catalog::noticed, this, [this](const Core::SourceNotice& notice) {
        mainWindowInstance->setStatusText(Ui::SourceNoticeText(notice));
    });
    connect(&spotifyCatalog, &SpotifySource::Catalog::noticed, this, [this](const Core::SourceNotice& notice) {
        mainWindowInstance->setStatusText(Ui::SourceNoticeText(notice));
    });
    // Before WindowSet::restore() below, which takes in the jam window.
#ifdef QIYAA_HAVE_JAM
    Jam::HostOptions jamOptions;
    jamOptions.client.appVersion = QCoreApplication::applicationVersion();
    jamOptions.config = [this] {
        const JamSettings jam = settings.jam();
        return Jam::HostConfig{jam.server, jam.waveFeedback, jam.shareAudio};
    };
    // Next to the settings: a test's or a read-only run's jam stays in its own directory.
    const Jam::SessionStore jamStore(settings.directory() + QStringLiteral("/jam-session.json"));
    jamHostSession = std::make_unique<Jam::HostSession>(&jamMode, &yandexLibrary, jamStore, std::move(jamOptions));
    connect(&networkWatch, &NetworkWatch::back, jamHostSession.get(), &Jam::HostSession::networkBack);  // HOST-26
    jamWindowInstance = std::make_unique<Ui::JamWindow>(jamHostSession.get(), &catalog, currentSkin.get());
    windowSet->add(
        jamWindowInstance.get(),
        {.group = QStringLiteral("jamWindow"),
         .shownAtStart = false,
         .raisedWhenShown = true,
         .stepsByDefault = QSize(1, 8)}
    );
#endif
    jamBridge = std::make_unique<JamBridge>(JamBridge::Parts{
        jamHostSession.get(),
        jamWindowInstance.get(),
        &jamMode,
        &corePlayer,
        &settings,
        windowSet.get(),
        accountControl.get(),
        mainWindowInstance.get(),
        playlistWindowInstance.get()
    });
    connect(jamBridge.get(), &JamBridge::statusText, mainWindowInstance.get(), &Ui::MainWindow::setStatusText);

    const MainWindowState mainState = settings.mainWindow();
    mainWindowInstance->setVolume(mainState.volume);
    mainWindowInstance->setBalance(mainState.balance);
    mainWindowInstance->setVisMode(static_cast<Ui::MainWindow::VisMode>(mainState.visMode));
    mainWindowInstance->setShowsRemainingTime(mainState.remainingTime);
    connect(mainWindowInstance.get(), &Ui::MainWindow::eqToggleRequested, this, [this] {
        windowSet->toggle(equalizerWindowInstance.get());
    });
    connect(mainWindowInstance.get(), &Ui::MainWindow::playlistToggleRequested, this, [this] {
        windowSet->toggle(playlistWindowInstance.get());
    });
    connect(mainWindowInstance.get(), &Ui::MainWindow::closeRequested, this, &Application::quit);

    const Audio::EqSettings eq = settings.equalizer();
    equalizerWindowInstance->setSettings(eq);
    equalizerWindowInstance->setAutoOn(settings.equalizerAuto());
    audioEngine.setEqualizer(eq);
    connect(
        equalizerWindowInstance.get(),
        &Ui::EqualizerWindow::settingsChanged,
        this,
        [this](const Audio::EqSettings& equalizerSettings) {
            audioEngine.setEqualizer(equalizerSettings);
            settings.setEqualizer(equalizerSettings);
        }
    );
    connect(
        equalizerWindowInstance.get(),
        &Ui::EqualizerWindow::statusText,
        mainWindowInstance.get(),
        &Ui::MainWindow::setStatusText
    );
    equalizerWindowInstance->setMixer(mainWindowInstance->volume(), mainWindowInstance->balance());
    connect(mainWindowInstance.get(), &Ui::MainWindow::volumeChanged, this, [this](int volume) {
        equalizerWindowInstance->setMixer(volume, mainWindowInstance->balance());
    });
    connect(mainWindowInstance.get(), &Ui::MainWindow::balanceChanged, this, [this](int balance) {
        equalizerWindowInstance->setMixer(mainWindowInstance->volume(), balance);
    });
    connect(
        equalizerWindowInstance.get(),
        &Ui::EqualizerWindow::volumeRequested,
        mainWindowInstance.get(),
        &Ui::MainWindow::setVolume
    );
    connect(
        equalizerWindowInstance.get(),
        &Ui::EqualizerWindow::balanceRequested,
        mainWindowInstance.get(),
        &Ui::MainWindow::setBalance
    );

    connect(windowSet.get(), &WindowSet::saveRequested, this, &Application::saveState);
    windowSet->restore();

    if (startOptions.mediaIntegration) {
        Integrations::MediaControls::Hooks hooks;
        hooks.volume = [this] { return mainWindowInstance->volume(); };
        hooks.setVolume = [this](int volume) { mainWindowInstance->setVolume(volume); };
        hooks.raise = [this] { windowSet->bringToFront(); };
        hooks.quit = [this] { quit(); };
        mediaControls =
            std::make_unique<Integrations::MediaControls>(&corePlayer, &trackSources, coverCache.get(), hooks);
        connect(
            mainWindowInstance.get(),
            &Ui::MainWindow::volumeChanged,
            mediaControls.get(),
            &Integrations::MediaControls::volumeChanged
        );
#if defined(QIYAA_HAVE_MPRIS)
        systemMediaControls = std::make_unique<Integrations::Mpris>(mediaControls.get());
#elif defined(QIYAA_HAVE_SMTC)
        systemMediaControls = std::make_unique<Integrations::Smtc>(mediaControls.get(), mainWindowInstance.get());
#elif defined(QIYAA_HAVE_MAC_MEDIA_CONTROLS)
        systemMediaControls = std::make_unique<Integrations::MacMediaControls>(mediaControls.get());
#endif
    }

    // The system's view of the network ends a wait for it at once (spec ERR-02); without a
    // backend that can tell, the Player retries on its timer alone.
    connect(&networkWatch, &NetworkWatch::onlineChanged, &corePlayer, &Core::Player::setNetworkOnline);
    if (startOptions.audio && !startOptions.offline) {
        networkWatch.start();
    }

    connect(qApp, &QApplication::aboutToQuit, this, [this] {
        saveState();
        corePlayer.stop();
    });

    telemetryBridge = std::make_unique<TelemetryBridge>(
        TelemetryBridge::Sources{
            this,
            &settings,
            accountControl.get(),
            windowSet.get(),
            &corePlayer,
            &audioEngine,
            &apiClient,
            mainWindowInstance.get(),
            equalizerWindowInstance.get(),
            milkdropWindowInstance.get(),
            jamHostSession.get(),
            &lyricsFinder,
            lyricsWindowInstance.get(),
            spotifyAccountControl.get(),
            &spotifyClientSession,
            &spotifyWeb
        },
        TelemetryBridge::Config{
            startOptions.telemetryUrl, networkManager, startOptions.skinOverride, startOptions.audio, audioFailed
        }
    );
    mainMenu = std::make_unique<MainMenu>(MainMenu::Parts{
        &corePlayer,
        &audioEngine,
        &yandexLibrary,
        &catalog,
        &localCatalog,
        &trackSources,
        &lyricsFinder,
        &yandexSource,
        &settings,
        windowSet.get(),
        accountControl.get(),
        spotifyAccountControl.get(),
        &spotifyCatalog,
        &spotifySource,
        jamBridge.get(),
        telemetryBridge.get(),
        &translations,
        mainWindowInstance.get(),
        playlistWindowInstance.get(),
        searchWindowInstance.get()
    });
    connect(mainMenu.get(), &MainMenu::skinChosen, this, &Application::loadSkin);
    connect(mainMenu.get(), &MainMenu::languageChosen, this, &Application::setLanguage);
    connect(mainMenu.get(), &MainMenu::visModeChosen, this, &Application::saveState);
    connect(mainMenu.get(), &MainMenu::quitChosen, this, &Application::quit);
}

Application::~Application() = default;

void Application::start() {
    windowSet->showAtStart();
    telemetryBridge->start();

    if (startOptions.offline) {
        return;
    }
    jamBridge->offerStored();
    accountControl->start();
    spotifyAccountControl->start();
}

bool Application::loadSkin(const QString& path) {
    std::unique_ptr<Skins::Skin> skin;
    try {
        skin = std::make_unique<Skins::Skin>(Skins::Skin::LoadFile(path, &baseSkin));
    } catch (const Skins::Error& error) {
        mainWindowInstance->setStatusText(tr("Cannot load the skin: %1").arg(QString::fromUtf8(error.what())));
        Q_EMIT skinFailed();
        return false;
    }
    // Swap after the windows point at the new skin.
    windowSet->setSkin(skin.get());
    currentSkin = std::move(skin);
    settings.setSkin(path);
    Q_EMIT skinChanged(path);
    return true;
}

void Application::setLanguage(Language language) {
    // Qt sends LanguageChange to every widget; the windows draw their texts again.
    translations.apply(language);
    settings.setLanguage(language);
    Q_EMIT languageChanged(language);
}

void Application::quit() {
    if (quitting) {
        return;
    }
    quitting = true;
    saveState();
    corePlayer.shutDown();
    windowSet->hideAll();
    // Queued, so a quit() from inside a nested loop (a menu) lands in the main one.
    const auto finish = [] { QMetaObject::invokeMethod(qApp, &QApplication::quit, Qt::QueuedConnection); };
    telemetryBridge->finish();
    if (apiClient.pendingPosts() == 0 && !telemetryBridge->isSending()) {
        return finish();
    }
    connect(telemetryBridge.get(), &TelemetryBridge::sent, this, [this, finish] {
        if (apiClient.pendingPosts() == 0) {
            finish();
        }
    });
    connect(&apiClient, &Yandex::ApiClient::postsSettled, this, [this, finish] {
        if (!telemetryBridge->isSending()) {
            finish();
        }
    });
    QTimer::singleShot(1500, this, finish);
}

void Application::saveState() {
    settings.setMainWindow(MainWindowState{
        mainWindowInstance->volume(),
        mainWindowInstance->balance(),
        static_cast<int>(mainWindowInstance->visMode()),
        mainWindowInstance->showsRemainingTime()
    });
    settings.setEqualizerAuto(equalizerWindowInstance->autoOn());
    windowSet->savePositions();
}

}  // namespace App
