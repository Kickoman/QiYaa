#include "app/application.h"

#include "app/account.h"
#include "app/jam_bridge.h"
#include "app/paths.h"
#include "app/window_set.h"
#include "audio/equalizer.h"
#include "core/cover_cache.h"
#include "integrations/media_controls.h"
#include "jam/host_session.h"
#include "skins/error.h"
#include "sources/yandex/history_wave.h"
#include "sources/yandex/tracks.h"
#include "telemetry/machine_id.h"
#include "telemetry/reporter.h"
#include "telemetry/system_info.h"
#include "ui/equalizer_window.h"
#include "ui/gen_window.h"
#include "ui/jam_window.h"
#include "ui/library_menu.h"
#include "ui/login_dialog.h"
#include "ui/main_window.h"
#include "ui/milkdrop_window.h"
#include "ui/now_playing_window.h"
#include "ui/playlist_window.h"
#include "ui/source_notices.h"
#include "yandex/file_info.h"
#include "yandex/token.h"
#ifdef QIYAA_HAVE_MPRIS
#include "integrations/mpris.h"
#endif
#ifdef QIYAA_HAVE_SMTC
#include "integrations/smtc.h"
#endif
#ifdef QIYAA_HAVE_MAC_MEDIA_CONTROLS
#include "integrations/mac_media_controls.h"
#endif

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFileDialog>
#include <QJsonObject>
#include <QKeySequence>
#include <QLatin1String>
#include <QList>
#include <QMenu>
#include <QNetworkInformation>
#include <QPoint>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <utility>

namespace App {

using Audio::EqSettings;

namespace {

// The names telemetry gives (spec/telemetry): codes, never a path or a text.
QString SkinName(const QString& path) {
    const QString prefix = QStringLiteral(":/skins/");
    if (path.isEmpty() || path == prefix + QStringLiteral("base-2.91.wsz")) {
        return QStringLiteral("base");
    }
    if (path.startsWith(prefix) && path.endsWith(QLatin1String(".wsz"))) {
        return path.mid(prefix.size()).chopped(4);
    }
    return QStringLiteral("custom");
}

QString VisModeCode(Ui::MainWindow::VisMode mode) {
    switch (mode) {
        case Ui::MainWindow::VisMode::Spectrum: return QStringLiteral("spectrum");
        case Ui::MainWindow::VisMode::Oscilloscope: return QStringLiteral("oscilloscope");
        case Ui::MainWindow::VisMode::Off: break;
    }
    return QStringLiteral("off");
}

QString FailureCode(Core::FailureKind kind) {
    switch (kind) {
        case Core::FailureKind::Network: return QStringLiteral("network");
        case Core::FailureKind::Auth: return QStringLiteral("auth");
        case Core::FailureKind::Track: break;
    }
    return QStringLiteral("track");
}

QString RequestFailureCode(Yandex::RequestError::Kind kind) {
    switch (kind) {
        case Yandex::RequestError::Kind::Network: return QStringLiteral("network");
        case Yandex::RequestError::Kind::Http: return QStringLiteral("http");
        case Yandex::RequestError::Kind::Content:
        case Yandex::RequestError::Kind::None: break;
    }
    return QStringLiteral("content");
}

#ifdef QIYAA_HAVE_MILKDROP
QString MilkdropFailureCode(Vis::MilkdropFailure failure) {
    switch (failure) {
        case Vis::MilkdropFailure::NoOpenGl: return QStringLiteral("no-opengl");
        case Vis::MilkdropFailure::OpenGlEs: return QStringLiteral("gles");
        case Vis::MilkdropFailure::OldOpenGl: return QStringLiteral("old-opengl");
        case Vis::MilkdropFailure::ProjectM: break;
    }
    return QStringLiteral("projectm");
}
#endif

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
    , yandexSource(&yandexLibrary)
    , corePlayer(&audioEngine, &trackSources)
    , catalog(&corePlayer, &yandexLibrary)
    , jamMode(&corePlayer) {
    trackSources.add(YandexSource::kSourceName, &yandexSource);
    firstRun = settings.wasEmpty();
    // Before any window: they take their texts from the translation.
    translations.apply(options.language.value_or(settings.language().value_or(kDefaultLanguage)));
    corePlayer.setShuffleAlgorithm(settings.shuffleAlgorithm());
    yandexSource.setQuality(settings.quality());
    corePlayer.setContinueWithVibeByDefault(settings.continueWithVibe());
    corePlayer.setHistoryWaveFactory(YandexSource::HistoryWaves(&yandexLibrary));
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
    const std::initializer_list<QWidget*> windowsWithShortcuts{
        mainWindowInstance.get(),
        equalizerWindowInstance.get(),
        playlistWindowInstance.get(),
        nowPlayingWindowInstance.get(),
    };
    for (QWidget* widget : windowsWithShortcuts) {
        installShortcuts(widget);
    }
    windowSet = std::make_unique<WindowSet>(
        &settings,
        mainWindowInstance.get(),
        WindowSet::Spec{.group = QStringLiteral("mainWindow"), .place = WindowSet::Place::Main, .keepsShade = true}
    );
    windowSet->add(
        equalizerWindowInstance.get(),
        {.group = QStringLiteral("equalizer"),
         .place = WindowSet::Place::Stacked,
         .visibleByDefault = true,
         .keepsShade = true,
         .button = WindowSet::Button::Equalizer}
    );
    windowSet->add(
        playlistWindowInstance.get(),
        {.group = QStringLiteral("playlist"),
         .place = WindowSet::Place::Stacked,
         .visibleByDefault = true,
         .keepsShade = true,
         .stepsByDefault = QSize(0, 4),
         .button = WindowSet::Button::Playlist}
    );
    windowSet->add(
        nowPlayingWindowInstance.get(), {.group = QStringLiteral("nowPlaying"), .stepsByDefault = QSize(0, 0)}
    );
    accountControl = std::make_unique<Account>(
        Account::Parts{&apiClient, &yandexLibrary, &corePlayer, &catalog, networkManager, mainWindowInstance.get()},
        Account::TokenFiles{TokenFile(), YaampTokenFiles(), ConfigDirectory()}
    );
    connect(accountControl.get(), &Account::statusText, mainWindowInstance.get(), &Ui::MainWindow::setStatusText);
    connect(accountControl.get(), &Account::failed, this, [this] {
        telemetryError(QStringLiteral("login"), QStringLiteral("failed"));
    });
#if defined(QIYAA_HAVE_MILKDROP)
    {
        const QString userPresets =
            (temporaryDirectory ? temporaryDirectory->path() : App::ConfigDirectory()) + QStringLiteral("/milkdrop");
        milkdropWindowInstance = std::make_unique<Ui::MilkdropWindow>(
            &audioEngine, QStringLiteral(":/milkdrop"), userPresets, currentSkin.get()
        );
        installShortcuts(milkdropWindowInstance.get());
        windowSet->add(
            milkdropWindowInstance.get(),
            {.group = QStringLiteral("milkdrop"),
             .place = WindowSet::Place::BelowRightOfMain,
             .stepsByDefault = QSize(0, 4)}
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
            &Ui::MilkdropWindow::presetChanged,
            this,
            [this](const QString& name, Ui::MilkdropWindow::PresetOrigin origin) {
                if (origin == Ui::MilkdropWindow::PresetOrigin::User) {
                    mainWindowInstance->setStatusText(QStringLiteral("Milkdrop: ") + name);
                }
            }
        );
        connect(milkdropWindowInstance.get(), &Ui::MilkdropWindow::transportKey, this, &Application::transportKey);
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
    installShortcuts(jamWindowInstance.get());
    windowSet->add(
        jamWindowInstance.get(),
        {.group = QStringLiteral("jamWindow"),
         .shownAtStart = false,
         .raisedWhenShown = true,
         .stepsByDefault = QSize(1, 8)}
    );
    connect(jamHostSession.get(), &Jam::HostSession::refused, this, [this](const QString& reason) {
        static const QRegularExpression code(QStringLiteral("^[a-z0-9-]{1,32}$"));
        telemetryError(QStringLiteral("jam"), code.match(reason).hasMatch() ? reason : QStringLiteral("other"));
    });
    connect(jamHostSession.get(), &Jam::HostSession::ended, this, [this](Jam::HostEnd why) {
        switch (why) {
            case Jam::HostEnd::Gone: telemetryError(QStringLiteral("jam"), QStringLiteral("gone")); break;
            case Jam::HostEnd::Expired: telemetryError(QStringLiteral("jam"), QStringLiteral("expired")); break;
            case Jam::HostEnd::ByServer:
                telemetryError(QStringLiteral("jam"), QStringLiteral("ended-by-server"));
                break;
            case Jam::HostEnd::ByHost: break;
        }
    });
    connect(jamHostSession.get(), &Jam::HostSession::activeChanged, this, [this](bool active) {
        telemetryFeature(QStringLiteral("jam"), active ? QStringLiteral("on") : QStringLiteral("off"));
#ifdef QIYAA_HAVE_TELEMETRY
        if (active && telemetryReporter) {
            telemetryReporter->countJam();
        }
#endif
    });
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
    connect(mainWindowInstance.get(), &Ui::MainWindow::menuRequested, this, [this](QPoint pos) {
        showMainMenu(pos, corePlayer.currentTrack());
    });
    connect(mainWindowInstance.get(), &Ui::MainWindow::sourcesMenuRequested, this, &Application::showSourcesMenu);
    connect(mainWindowInstance.get(), &Ui::MainWindow::closeRequested, this, &Application::quit);

    const Audio::EqSettings eq = settings.equalizer();
    equalizerWindowInstance->setSettings(eq);
    equalizerWindowInstance->setAutoOn(settings.equalizerAuto());
    audioEngine.setEqualizer(eq);
    connect(
        equalizerWindowInstance.get(),
        &Ui::EqualizerWindow::settingsChanged,
        this,
        [this, wasEnabled = eq.enabled](const Audio::EqSettings& equalizerSettings) mutable {
            audioEngine.setEqualizer(equalizerSettings);
            settings.setEqualizer(equalizerSettings);
            if (equalizerSettings.enabled != wasEnabled) {
                wasEnabled = equalizerSettings.enabled;
                telemetryFeature(
                    QStringLiteral("equalizer"), wasEnabled ? QStringLiteral("on") : QStringLiteral("off")
                );
            }
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

    connect(playlistWindowInstance.get(), &Ui::PlaylistWindow::sourcesMenuRequested, this, [this](QPoint pos) {
        showMainMenu(pos, corePlayer.currentTrack());
    });
    connect(playlistWindowInstance.get(), &Ui::PlaylistWindow::trackMenuRequested, this, [this](QPoint pos, int row) {
        const auto& entries = corePlayer.entries();
        showMainMenu(pos, row >= 0 && row < entries.size() ? &entries[row].track : nullptr);
    });

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

    // Telemetry (spec/telemetry): categories only; nothing happens without a reporter.
    connect(&corePlayer, &Core::Player::failed, this, [this](Core::FailureKind kind) {
        telemetryError(QStringLiteral("playback"), FailureCode(kind));
    });
    connect(&audioEngine, &Audio::AudioEngine::errorOccurred, this, [this] {
        telemetryError(QStringLiteral("audio"), QStringLiteral("device"));
    });
    connect(&audioEngine, &Audio::AudioEngine::streamUndecodable, this, [this] {
        telemetryError(QStringLiteral("audio"), QStringLiteral("undecodable"));
    });
    connect(
        &apiClient,
        &Yandex::ApiClient::requestFailed,
        this,
        [this](Yandex::RequestError::Kind kind, int httpStatus) {
            telemetryError(
                QStringLiteral("api"),
                RequestFailureCode(kind),
                kind == Yandex::RequestError::Kind::Http ? httpStatus : 0
            );
        }
    );
    connect(mainWindowInstance.get(), &Ui::MainWindow::visModeChanged, this, [this](Ui::MainWindow::VisMode mode) {
        telemetryFeature(QStringLiteral("vis"), VisModeCode(mode));
    });
#ifdef QIYAA_HAVE_TELEMETRY
    connect(&corePlayer, &Core::Player::currentTrackChanged, this, [this] {
        if (telemetryReporter && corePlayer.currentTrack()) {
            telemetryReporter->countTrack();
        }
    });
    connect(&audioEngine, &Audio::AudioEngine::stateChanged, this, [this](Audio::AudioEngine::State state) {
        if (telemetryReporter) {
            telemetryReporter->setPlaying(state == Audio::AudioEngine::State::Playing);
        }
    });
#endif
    connect(windowSet.get(), &WindowSet::visibilityChosen, this, [this](Ui::SkinnedWindow* window, bool on) {
        if (window != milkdropWindowInstance.get()) {
            return;
        }
        telemetryFeature(QStringLiteral("milkdrop"), on ? QStringLiteral("on") : QStringLiteral("off"));
#ifdef QIYAA_HAVE_TELEMETRY
        if (telemetryReporter) {
            telemetryReporter->setMilkdropShown(on);
        }
#endif
    });
#ifdef QIYAA_HAVE_MILKDROP
    if (milkdropWindowInstance) {
        connect(milkdropWindowInstance.get(), &Ui::MilkdropWindow::failed, this, [this](Vis::MilkdropFailure failure) {
            telemetryError(QStringLiteral("milkdrop"), MilkdropFailureCode(failure));
        });
    }
#endif
}

Application::~Application() = default;

void Application::start() {
    windowSet->showAtStart();
    startTelemetry();

    if (startOptions.offline) {
        return;
    }
    jamBridge->offerStored();
    accountControl->start();
}

bool Application::loadSkin(const QString& path) {
    std::unique_ptr<Skins::Skin> skin;
    try {
        skin = std::make_unique<Skins::Skin>(Skins::Skin::LoadFile(path, &baseSkin));
    } catch (const Skins::Error& error) {
        mainWindowInstance->setStatusText(tr("Cannot load the skin: %1").arg(QString::fromUtf8(error.what())));
        telemetryError(QStringLiteral("skin"), QStringLiteral("load"));
        return false;
    }
    // Swap after the windows point at the new skin.
    windowSet->setSkin(skin.get());
    currentSkin = std::move(skin);
    settings.setSkin(path);
    telemetryFeature(QStringLiteral("skin"), SkinName(path));
    return true;
}

void Application::setLanguage(Language language) {
    // Qt sends LanguageChange to every widget; the windows draw their texts again.
    translations.apply(language);
    settings.setLanguage(language);
    telemetryFeature(QStringLiteral("language"), LanguageCode(language));
}

bool Application::telemetryEnabled() const {
    return settings.telemetryEnabled();
}

void Application::setTelemetryEnabled(bool on) {
    settings.setTelemetryEnabled(on);
#ifdef QIYAA_HAVE_TELEMETRY
    if (on) {
        startTelemetry();
        return;
    }
    if (telemetryReporter) {
        telemetryReporter->forget();
        telemetryReporter.reset();
    }
    Telemetry::Reporter::ForgetAll(telemetryDirectory());
#endif
}

QString Application::telemetryDirectory() const {
    return settings.directory() + QStringLiteral("/telemetry");
}

void Application::startTelemetry() {
#ifdef QIYAA_HAVE_TELEMETRY
    if (telemetryReporter || startOptions.telemetryUrl.isEmpty()) {
        return;
    }
    if (!telemetryEnabled()) {
        Telemetry::Reporter::ForgetAll(telemetryDirectory());  // TEL-02: nothing left to send
        return;
    }
    const QString version = QCoreApplication::applicationVersion();
    const auto storeMachine = [this](const QString& id) { settings.setMachine(id); };
    telemetryReporter = std::make_unique<Telemetry::Reporter>(Telemetry::Reporter::Config{
        startOptions.telemetryUrl,
        telemetryDirectory(),
        version.isEmpty() ? QStringLiteral("dev") : version,
        Telemetry::MachineIdFor(settings.machine(), storeMachine),
        networkManager
    });
    static const QRegularExpression notBackend(QStringLiteral("[^A-Za-z0-9 _-]"));
    const bool milkdropVisible = milkdropWindowInstance && milkdropWindowInstance->isVisible();
    QJsonObject fields = Telemetry::SystemFields();
    fields.insert(QStringLiteral("language"), LanguageCode(language()));
    fields.insert(QStringLiteral("scale"), std::clamp(mainWindowInstance->scale(), 1.0, 4.0));
    fields.insert(
        QStringLiteral("skin"),
        SkinName(startOptions.skinOverride.isEmpty() ? settings.skin() : startOptions.skinOverride)
    );
    fields.insert(QStringLiteral("vis"), VisModeCode(mainWindowInstance->visMode()));
    fields.insert(QStringLiteral("equalizer"), equalizerWindowInstance->settings().enabled);
    fields.insert(QStringLiteral("milkdropBuilt"), milkdropWindowInstance != nullptr);
    fields.insert(QStringLiteral("milkdropVisible"), milkdropVisible);
    fields.insert(QStringLiteral("jamBuilt"), jamHostSession != nullptr);
    fields.insert(
        QStringLiteral("audioBackend"),
        audioFailed || !startOptions.audio ? QString() : audioEngine.backendName().remove(notBackend).left(24)
    );
    fields.insert(QStringLiteral("loggedIn"), !Yandex::FindToken(TokenFile(), YaampTokenFiles()).token.isEmpty());
    fields.insert(QStringLiteral("firstRun"), firstRun);
    if (!settings.hasFirstStart()) {
        settings.setFirstStart(QDate::currentDate());
    }
    telemetryReporter->begin(fields);
    telemetryReporter->setMilkdropShown(milkdropVisible);
    telemetryReporter->setPlaying(audioEngine.state() == Audio::AudioEngine::State::Playing);
    if (audioFailed) {
        telemetryError(QStringLiteral("audio"), QStringLiteral("init"));
    }
#endif
}

void Application::telemetryError(
    [[maybe_unused]] const QString& area,
    [[maybe_unused]] const QString& kind,
    [[maybe_unused]] int httpStatus
) {
#ifdef QIYAA_HAVE_TELEMETRY
    if (telemetryReporter) {
        telemetryReporter->recordError(area, kind, httpStatus);
    }
#endif
}

void Application::telemetryFeature([[maybe_unused]] const QString& name, [[maybe_unused]] const QString& value) {
#ifdef QIYAA_HAVE_TELEMETRY
    if (telemetryReporter) {
        telemetryReporter->recordFeature(name, value);
    }
#endif
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
    bool telemetrySending = false;
#ifdef QIYAA_HAVE_TELEMETRY
    if (telemetryReporter) {
        telemetryReporter->finish();
        telemetrySending = telemetryReporter->isSending();
        connect(telemetryReporter.get(), &Telemetry::Reporter::sendingDone, this, [this, finish] {
            if (apiClient.pendingPosts() == 0) {
                finish();
            }
        });
    }
#endif
    if (apiClient.pendingPosts() == 0 && !telemetrySending) {
        return finish();
    }
    connect(&apiClient, &Yandex::ApiClient::postsSettled, this, [this, finish] {
#ifdef QIYAA_HAVE_TELEMETRY
        if (telemetryReporter && telemetryReporter->isSending()) {
            return;
        }
#endif
        finish();
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

void Application::installShortcuts(QWidget* widget) {
    auto add = [widget](const QKeySequence& shortcut, auto handler) {
        auto* action = new QAction(widget);
        action->setShortcut(shortcut);
        QObject::connect(action, &QAction::triggered, widget, handler);
        widget->addAction(action);
    };
    for (int key : {Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V, Qt::Key_B, Qt::Key_Left, Qt::Key_Right}) {
        add(QKeySequence(key), [this, key] { transportKey(key); });
    }
    add(QKeySequence(Qt::ALT | Qt::Key_G), [this] { windowSet->toggle(equalizerWindowInstance.get()); });
    add(QKeySequence(Qt::ALT | Qt::Key_E), [this] { windowSet->toggle(playlistWindowInstance.get()); });
    add(QKeySequence(Qt::CTRL | Qt::Key_D), [this] {
        windowSet->setScale(std::abs(mainWindowInstance->scale() - 2.0) < 1e-6 ? 1.0 : 2.0);
    });
    add(QKeySequence(Qt::CTRL | Qt::Key_W), [this] { mainWindowInstance->setShaded(!mainWindowInstance->isShaded()); });
    add(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K), [this] { windowSet->toggle(milkdropWindowInstance.get()); });
}

void Application::transportKey(int key) {
    switch (key) {
        case Qt::Key_Z: corePlayer.previous(); break;
        case Qt::Key_X: corePlayer.play(); break;
        case Qt::Key_C: corePlayer.pause(); break;
        case Qt::Key_V: corePlayer.stop(); break;
        case Qt::Key_B: corePlayer.next(); break;
        case Qt::Key_Left: corePlayer.seekTo(audioEngine.positionSeconds() - 5); break;
        case Qt::Key_Right: corePlayer.seekTo(audioEngine.positionSeconds() + 5); break;
        default: break;
    }
}

void Application::fillWindowActions(QMenu* menu) {
    QAction* equalizerAction = menu->addAction(tr("Equalizer"), this, [this](bool on) {
        windowSet->setVisible(equalizerWindowInstance.get(), on);
    });
    equalizerAction->setCheckable(true);
    equalizerAction->setChecked(equalizerWindowInstance->isVisible());
    equalizerAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_G));
    QAction* playlistAction = menu->addAction(tr("Playlist"), this, [this](bool on) {
        windowSet->setVisible(playlistWindowInstance.get(), on);
    });
    playlistAction->setCheckable(true);
    playlistAction->setChecked(playlistWindowInstance->isVisible());
    playlistAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_E));
    QAction* nowPlayingAction = menu->addAction(tr("Now playing"), this, [this](bool on) {
        windowSet->setVisible(nowPlayingWindowInstance.get(), on);
    });
    nowPlayingAction->setCheckable(true);
    nowPlayingAction->setChecked(nowPlayingWindowInstance->isVisible());
    if (milkdropWindowInstance) {
        QAction* milkdropAction = menu->addAction(QStringLiteral("Milkdrop"), this, [this](bool on) {
            windowSet->setVisible(milkdropWindowInstance.get(), on);
        });
        milkdropAction->setCheckable(true);
        milkdropAction->setChecked(milkdropWindowInstance->isVisible());
        milkdropAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K));
    }

    QMenu* visualizationMenu = menu->addMenu(tr("Visualization"));
    auto* visualizationGroup = new QActionGroup(visualizationMenu);
    const std::pair<Ui::MainWindow::VisMode, QString> modes[] = {
        {Ui::MainWindow::VisMode::Spectrum, tr("Spectrum")},
        {Ui::MainWindow::VisMode::Oscilloscope, tr("Oscilloscope")},
        {Ui::MainWindow::VisMode::Off, tr("Off")}
    };
    for (const auto& [mode, name] : modes) {
        QAction* action = visualizationMenu->addAction(name, this, [this, mode] {
            mainWindowInstance->setVisMode(mode);
            saveState();
        });
        action->setCheckable(true);
        action->setChecked(mainWindowInstance->visMode() == mode);
        visualizationGroup->addAction(action);
    }

    QMenu* shuffleMenu = menu->addMenu(tr("Shuffle"));
    shuffleMenu->setObjectName(QStringLiteral("shuffleMenu"));
    auto* shuffleGroup = new QActionGroup(shuffleMenu);
    const std::pair<Core::ShuffleAlgorithm, QString> algorithms[] = {
        {Core::ShuffleAlgorithm::Random, tr("Random track")},
        {Core::ShuffleAlgorithm::WithoutRepeats, tr("Without repeats")}
    };
    for (const auto& [algorithm, name] : algorithms) {
        QAction* action = shuffleMenu->addAction(name, this, [this, algorithm] {
            corePlayer.setShuffleAlgorithm(algorithm);
        });
        action->setCheckable(true);
        action->setChecked(corePlayer.shuffleAlgorithm() == algorithm);
        shuffleGroup->addAction(action);
    }
    QAction* vibeAction = menu->addAction(tr("Continue with a vibe"), this, [this](bool on) {
        corePlayer.setContinueWithVibe(on);  // CONT-02
    });
    vibeAction->setObjectName(QStringLiteral("continueWithVibe"));
    vibeAction->setCheckable(true);
    vibeAction->setChecked(corePlayer.continuesWithVibe());
    vibeAction->setEnabled(!corePlayer.hasOwnContinuation());

    QMenu* qualityMenu = menu->addMenu(tr("Quality"));
    qualityMenu->setObjectName(QStringLiteral("qualityMenu"));
    auto* qualityGroup = new QActionGroup(qualityMenu);
    const std::pair<Yandex::Quality, QString> qualities[] = {
        {Yandex::Quality::Standard, tr("Standard")}, {Yandex::Quality::Economy, tr("Economy: a third of the data")}
    };
    for (const auto& [quality, name] : qualities) {
        QAction* action = qualityMenu->addAction(name, this, [this, quality] {
            yandexSource.setQuality(quality);
            settings.setQuality(quality);
        });
        action->setCheckable(true);
        action->setChecked(yandexSource.quality() == quality);
        qualityGroup->addAction(action);
    }

    QMenu* skinsMenu = menu->addMenu(tr("Skins"));
    const QStringList builtin =
        QDir(QStringLiteral(":/skins")).entryList({QStringLiteral("*.wsz")}, QDir::Files, QDir::Name);
    for (const QString& name : builtin) {
        const QString path = QStringLiteral(":/skins/") + name;
        skinsMenu->addAction(QString(name).chopped(4), this, [this, path] { loadSkin(path); });
    }
    skinsMenu->addSeparator();
    skinsMenu->addAction(tr("Load a skin…"), this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            mainWindowInstance.get(), tr("Winamp skin"), QDir::homePath(), tr("Winamp skins (*.wsz *.zip)")
        );
        if (!path.isEmpty()) {
            loadSkin(path);
        }
    });

    QMenu* sizeMenu = menu->addMenu(tr("Size"));
    auto* sizeGroup = new QActionGroup(sizeMenu);
    for (double scaleFactor : {1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0}) {
        QAction* action =
            sizeMenu->addAction(QStringLiteral("%1%").arg(qRound(scaleFactor * 100)), this, [this, scaleFactor] {
                windowSet->setScale(scaleFactor);
            });
        action->setCheckable(true);
        action->setChecked(std::abs(mainWindowInstance->scale() - scaleFactor) < 1e-6);
        sizeGroup->addAction(action);
    }
    sizeMenu->addSeparator();
    QAction* doubleSizeAction = sizeMenu->addAction(tr("Double size"), this, [this] {
        windowSet->setScale(std::abs(mainWindowInstance->scale() - 2.0) < 1e-6 ? 1.0 : 2.0);
    });
    doubleSizeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));

    // Named in each language, and with the English word, so that a wrong pick can be undone.
    QString languageTitle = tr("Language");
    if (languageTitle != QLatin1String("Language")) {
        languageTitle += QStringLiteral(" (Language)");
    }
    QMenu* languageMenu = menu->addMenu(languageTitle);
    auto* languageGroup = new QActionGroup(languageMenu);
    for (Language language : Languages()) {
        QAction* action = languageMenu->addAction(LanguageName(language), this, [this, language] {
            setLanguage(language);
        });
        action->setCheckable(true);
        action->setChecked(language == translations.language());
        languageGroup->addAction(action);
    }

    QAction* alwaysOnTopAction = menu->addAction(tr("Always on top"), this, [this](bool on) {
        windowSet->setAlwaysOnTop(on);
    });
    alwaysOnTopAction->setCheckable(true);
    alwaysOnTopAction->setChecked(mainWindowInstance->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
#ifdef QIYAA_HAVE_TELEMETRY
    if (!startOptions.telemetryUrl.isEmpty()) {
        QAction* telemetryAction = menu->addAction(tr("Send statistics"), this, [this](bool on) {
            setTelemetryEnabled(on);
        });
        telemetryAction->setCheckable(true);
        telemetryAction->setChecked(telemetryEnabled());
    }
#endif
}

void Application::showSourcesMenu(QPoint globalPosition) {
    auto* menu = new QMenu(mainWindowInstance.get());
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::AddLibraryActions(
        menu,
        &yandexLibrary,
        &catalog,
        &trackSources,
        corePlayer.currentTrack(),
        mainWindowInstance.get(),
        [this] { accountControl->login(); }
    );
    menu->popup(globalPosition);
}

void Application::showMainMenu(QPoint globalPosition, const Core::Track* track) {
    auto* menu = new QMenu(mainWindowInstance.get());
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::AddLibraryActions(menu, &yandexLibrary, &catalog, &trackSources, track, mainWindowInstance.get(), [this] {
        accountControl->login();
    });
    menu->addSeparator();
    jamBridge->addMenu(menu);
    fillWindowActions(menu);
    menu->addSeparator();
    if (yandexLibrary.isLoggedIn()) {
        menu->addAction(
            tr("Log out (%1)").arg(yandexLibrary.account().displayName), accountControl.get(), &Account::logout
        );
    }
    menu->addAction(tr("Quit QiYaa"), this, &Application::quit);
    menu->popup(globalPosition);
}

}  // namespace App
