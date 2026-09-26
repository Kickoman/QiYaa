#include "app/application.h"

#include "app/paths.h"
#include "audio/equalizer.h"
#include "core/cover_cache.h"
#include "integrations/media_controls.h"
#include "skins/error.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QMenu>
#include <QPainter>
#include <QScreen>
#include <QTimer>

#include <algorithm>
#include <cmath>
#ifdef QIYAA_HAVE_MPRIS
#include "integrations/mpris.h"
#endif
#ifdef QIYAA_HAVE_SMTC
#include "integrations/smtc.h"
#endif
#include "ui/equalizer_window.h"
#include "ui/gen_window.h"
#include "ui/library_menu.h"
#include "ui/login_dialog.h"
#include "ui/main_window.h"
#include "ui/milkdrop_window.h"  // stays null without QIYAA_HAVE_MILKDROP
#include "ui/now_playing_window.h"
#include "ui/playlist_window.h"
#include "ui/snap.h"
#include "yandex/token.h"

namespace App {

using Audio::EqSettings;

namespace {

QString SettingsPath(const Application::Options& o, QTemporaryDir* tmp) {
    if (tmp) {
        return tmp->filePath(QStringLiteral("settings.ini"));
    }
    return o.settingsFile.isEmpty() ? App::ConfigDir() + QStringLiteral("/settings.ini")
                                    : o.settingsFile;
}

Audio::EqSettings ReadEq(const QSettings& settings) {
    Audio::EqSettings eq;
    eq.enabled = settings.value(QStringLiteral("equalizer/enabled"), true).toBool();
    eq.preampDb = settings.value(QStringLiteral("equalizer/preamp"), 0.0).toDouble();
    const QStringList bands = settings.value(QStringLiteral("equalizer/bands")).toStringList();
    for (int i = 0; i < Audio::kEqBands && i < bands.size(); ++i) {
        eq.bandsDb[i] = bands[i].toDouble();
    }
    return eq;
}

void WriteEq(QSettings& settings, const Audio::EqSettings& eq) {
    settings.setValue(QStringLiteral("equalizer/enabled"), eq.enabled);
    settings.setValue(QStringLiteral("equalizer/preamp"), eq.preampDb);
    QStringList bands;
    for (double b : eq.bandsDb) {
        bands << QString::number(b, 'f', 1);
    }
    settings.setValue(QStringLiteral("equalizer/bands"), bands);
}

}  // namespace

Application::Application(const Options& options, QObject* parent)
    : QObject(parent)
    , startOptions(options)
    , tmpDir(options.readOnlySettings ? std::make_unique<QTemporaryDir>() : nullptr)
    , settings(SettingsPath(options, tmpDir.get()), QSettings::IniFormat)
    , baseSkin(Skins::Skin::BuiltinBase())
    , currentSkin(std::make_unique<Skins::Skin>(baseSkin))
    , apiClient(&networkManager)
    , yandexLibrary(&apiClient)
    , corePlayer(&yandexLibrary, &audioEngine) {
    if (startOptions.audio) {
        if (const Audio::AudioEngine::InitResult audio = audioEngine.init(); !audio.ok) {
            qWarning("Audio: %s", qPrintable(audio.message));
        } else {
            qInfo("Audio backend: %s", qPrintable(audioEngine.backendName()));
        }
    }

    const QString skinPath = options.skinOverride.isEmpty()
        ? settings.value(QStringLiteral("skin")).toString()
        : options.skinOverride;
    if (!skinPath.isEmpty()) {
        try {
            currentSkin = std::make_unique<Skins::Skin>(Skins::Skin::LoadFile(skinPath, &baseSkin));
        } catch (const Skins::Error& error) {
            qWarning("Skin: %s; using the built-in one", error.what());
        }
    }

    mainWindowInstance = std::make_unique<Ui::MainWindow>(&corePlayer, currentSkin.get());
    equalizerWindowInstance = std::make_unique<Ui::EqualizerWindow>(currentSkin.get());
    playlistWindowInstance = std::make_unique<Ui::PlaylistWindow>(&corePlayer, currentSkin.get());
    coverCache = std::make_unique<Core::CoverCache>(
        &networkManager, tmpDir ? tmpDir->filePath(QStringLiteral("covers")) : QString()
    );
    nowPlayingWindowInstance =
        std::make_unique<Ui::NowPlayingWindow>(&corePlayer, coverCache.get(), currentSkin.get());
    for (QWidget* widget : std::initializer_list<QWidget*>{
             mainWindowInstance.get(), equalizerWindowInstance.get(), playlistWindowInstance.get(),
             nowPlayingWindowInstance.get()
         }) {
        installShortcuts(widget);
    }
    equalizerWindowInstance->setSecondary();
    playlistWindowInstance->setSecondary();
    nowPlayingWindowInstance->setSecondary();
    nowPlayingWindowInstance->setSizeSteps(
        settings.value(QStringLiteral("nowPlaying/steps"), QSize(0, 0)).toSize()
    );
    connect(nowPlayingWindowInstance.get(), &Ui::NowPlayingWindow::closeRequested, this, [this] {
        setNowPlayingVisible(false);
    });
    connect(
        nowPlayingWindowInstance.get(), &Ui::GenWindow::sizeStepsChanged, this,
        [this](QSize size) { settings.setValue(QStringLiteral("nowPlaying/steps"), size); }
    );
#if defined(QIYAA_HAVE_MILKDROP)
    {
        const QString userPresets =
            (tmpDir ? tmpDir->path() : App::ConfigDir()) + QStringLiteral("/milkdrop");
        milkdropWindowInstance = std::make_unique<Ui::MilkdropWindow>(
            &audioEngine, QStringLiteral(":/milkdrop"), userPresets, currentSkin.get()
        );
        installShortcuts(milkdropWindowInstance.get());
        milkdropWindowInstance->setSecondary();
        milkdropWindowInstance->setSizeSteps(
            settings.value(QStringLiteral("milkdrop/steps"), QSize(0, 4)).toSize()
        );
        milkdropWindowInstance->setShuffle(
            settings.value(QStringLiteral("milkdrop/shuffle"), true).toBool()
        );
        milkdropWindowInstance->setLocked(
            settings.value(QStringLiteral("milkdrop/locked"), false).toBool()
        );
        milkdropWindowInstance->setPresetSeconds(
            settings.value(QStringLiteral("milkdrop/seconds"), 30).toInt()
        );
        milkdropWindowInstance->setBlackPresets(
            settings.value(QStringLiteral("milkdrop/black")).toStringList()
        );
        milkdropWindowInstance->selectPreset(
            settings.value(QStringLiteral("milkdrop/preset")).toString()
        );
        connect(milkdropWindowInstance.get(), &Ui::MilkdropWindow::closeRequested, this, [this] {
            setMilkdropVisible(false);
        });
        connect(
            milkdropWindowInstance.get(), &Ui::GenWindow::sizeStepsChanged, this,
            [this](QSize size) { settings.setValue(QStringLiteral("milkdrop/steps"), size); }
        );
        connect(milkdropWindowInstance.get(), &Ui::MilkdropWindow::settingsChanged, this, [this] {
            settings.setValue(
                QStringLiteral("milkdrop/shuffle"), milkdropWindowInstance->shuffle()
            );
            settings.setValue(QStringLiteral("milkdrop/locked"), milkdropWindowInstance->locked());
            settings.setValue(
                QStringLiteral("milkdrop/seconds"), milkdropWindowInstance->presetSeconds()
            );
            settings.setValue(
                QStringLiteral("milkdrop/preset"), milkdropWindowInstance->currentPreset()
            );
            settings.setValue(
                QStringLiteral("milkdrop/black"), milkdropWindowInstance->blackPresets()
            );
        });
        // Picked by hand: say which one it is (like Milkdrop's own title display).
        connect(
            milkdropWindowInstance.get(), &Ui::MilkdropWindow::presetChanged, this,
            [this](const QString& name, bool byUser) {
                if (byUser) {
                    mainWindowInstance->setStatusText(QStringLiteral("Milkdrop: ") + name);
                }
            }
        );
        connect(
            milkdropWindowInstance.get(), &Ui::MilkdropWindow::transportKey, this,
            &Application::transportKey
        );
        connect(
            &audioEngine, &Audio::AudioEngine::stateChanged, milkdropWindowInstance.get(),
            [this](Audio::AudioEngine::State state) {
                milkdropWindowInstance->setPlaying(state == Audio::AudioEngine::State::Playing);
            }
        );
    }
#endif

    // Main window.
    mainWindowInstance->setVolume(settings.value(QStringLiteral("volume"), 75).toInt());
    mainWindowInstance->setBalance(settings.value(QStringLiteral("balance"), 0).toInt());
    mainWindowInstance->setVisMode(Ui::MainWindow::VisMode(
        std::clamp(settings.value(QStringLiteral("vis/mode"), 0).toInt(), 0, 2)
    ));
    mainWindowInstance->setShowsRemainingTime(
        settings.value(QStringLiteral("time/remaining"), false).toBool()
    );
    connect(mainWindowInstance.get(), &Ui::MainWindow::eqToggleRequested, this, [this] {
        setEqualizerVisible(!equalizerWindowInstance->isVisible());
    });
    connect(mainWindowInstance.get(), &Ui::MainWindow::plToggleRequested, this, [this] {
        setPlaylistVisible(!playlistWindowInstance->isVisible());
    });
    connect(
        mainWindowInstance.get(), &Ui::MainWindow::menuRequested, this, &Application::showMainMenu
    );
    connect(
        mainWindowInstance.get(), &Ui::MainWindow::sourcesMenuRequested, this,
        &Application::showSourcesMenu
    );
    connect(mainWindowInstance.get(), &Ui::MainWindow::closeRequested, this, &Application::quit);
    // Minimising the main window takes the equalizer and playlist with it.
    connect(
        mainWindowInstance.get(), &Ui::MainWindow::minimizedChanged, this,
        [this](bool minimized) {
            if (minimized) {
                equalizerWindowInstance->hide();
                playlistWindowInstance->hide();
                nowPlayingWindowInstance->hide();
                if (milkdropWindowInstance) {
                    milkdropWindowInstance->hide();
                }
            } else {
                if (settings.value(QStringLiteral("equalizer/visible"), true).toBool()) {
                    equalizerWindowInstance->show();
                }
                if (settings.value(QStringLiteral("playlist/visible"), true).toBool()) {
                    playlistWindowInstance->show();
                }
                if (settings.value(QStringLiteral("nowPlaying/visible"), false).toBool()) {
                    nowPlayingWindowInstance->show();
                }
                if (milkdropWindowInstance
                    && settings.value(QStringLiteral("milkdrop/visible"), false).toBool()) {
                    milkdropWindowInstance->show();
                }
            }
        }
    );

    // Equalizer.
    const Audio::EqSettings eq = ReadEq(settings);
    equalizerWindowInstance->setSettings(eq);
    equalizerWindowInstance->setAutoOn(
        settings.value(QStringLiteral("equalizer/auto"), false).toBool()
    );
    audioEngine.setEqualizer(eq);
    connect(
        equalizerWindowInstance.get(), &Ui::EqualizerWindow::settingsChanged, this,
        [this](const Audio::EqSettings& equalizerSettings) {
            audioEngine.setEqualizer(equalizerSettings);
            WriteEq(settings, equalizerSettings);
        }
    );
    connect(
        equalizerWindowInstance.get(), &Ui::EqualizerWindow::statusText, mainWindowInstance.get(),
        &Ui::MainWindow::setStatusText
    );
    // The EQ's shade mode shows/controls the main window's volume and balance.
    equalizerWindowInstance->setMixer(mainWindowInstance->volume(), mainWindowInstance->balance());
    connect(mainWindowInstance.get(), &Ui::MainWindow::volumeChanged, this, [this](int value) {
        equalizerWindowInstance->setMixer(value, mainWindowInstance->balance());
    });
    connect(mainWindowInstance.get(), &Ui::MainWindow::balanceChanged, this, [this](int value) {
        equalizerWindowInstance->setMixer(mainWindowInstance->volume(), value);
    });
    connect(
        equalizerWindowInstance.get(), &Ui::EqualizerWindow::volumeRequested,
        mainWindowInstance.get(), &Ui::MainWindow::setVolume
    );
    connect(
        equalizerWindowInstance.get(), &Ui::EqualizerWindow::balanceRequested,
        mainWindowInstance.get(), &Ui::MainWindow::setBalance
    );
    connect(equalizerWindowInstance.get(), &Ui::EqualizerWindow::closeRequested, this, [this] {
        setEqualizerVisible(false);
    });

    // Playlist.
    playlistWindowInstance->setSizeSteps(
        settings.value(QStringLiteral("playlist/steps"), QSize(0, 4)).toSize()
    );
    connect(playlistWindowInstance.get(), &Ui::PlaylistWindow::closeRequested, this, [this] {
        setPlaylistVisible(false);
    });
    connect(
        playlistWindowInstance.get(), &Ui::PlaylistWindow::sourcesMenuRequested, this,
        &Application::showMainMenu
    );
    connect(
        playlistWindowInstance.get(), &Ui::PlaylistWindow::sizeStepsChanged, this,
        [this](QSize size) { settings.setValue(QStringLiteral("playlist/steps"), size); }
    );

    for (Ui::SkinnedWindow* window : windows()) {
        connect(window, &Ui::SkinnedWindow::moveFinished, this, &Application::saveState);
    }

    // Shade states: applied before the windows are placed, so saved positions match.
    const std::pair<Ui::SkinnedWindow*, QString> shades[] = {
        {mainWindowInstance.get(), QStringLiteral("mainWindow/shaded")},
        {equalizerWindowInstance.get(), QStringLiteral("equalizer/shaded")},
        {playlistWindowInstance.get(), QStringLiteral("playlist/shaded")}
    };
    for (const auto& [w, key] : shades) {
        w->setShaded(settings.value(key, false).toBool());
        connect(w, &Ui::SkinnedWindow::shadeChanged, this, [this, key](bool on) {
            settings.setValue(key, on);
            saveState();
        });
    }

    const double scale = settings.value(QStringLiteral("scale"), 1.0).toDouble();
    for (Ui::SkinnedWindow* window : windows()) {
        window->setScale(scale);
    }
    if (settings.value(QStringLiteral("alwaysOnTop"), false).toBool()) {
        setAlwaysOnTop(true);
    }

    if (startOptions.mediaIntegration) {
        Integrations::MediaControls::Hooks hooks;
        hooks.volume = [this] { return mainWindowInstance->volume(); };
        hooks.setVolume = [this](int value) { mainWindowInstance->setVolume(value); };
        hooks.raise = [this] {
            if (mainWindowInstance->isMinimized()) {
                mainWindowInstance->showNormal();
            }
            for (Ui::SkinnedWindow* window : windows()) {
                if (window->isVisible()) {
                    window->raise();
                }
            }
            mainWindowInstance->activateWindow();
        };
        hooks.quit = [this] { quit(); };
        mediaControls =
            std::make_unique<Integrations::MediaControls>(&corePlayer, coverCache.get(), hooks);
        connect(
            mainWindowInstance.get(), &Ui::MainWindow::volumeChanged, mediaControls.get(),
            &Integrations::MediaControls::volumeChanged
        );
#if defined(QIYAA_HAVE_MPRIS)
        systemMediaControls = std::make_unique<Integrations::Mpris>(mediaControls.get());
#elif defined(QIYAA_HAVE_SMTC)
        systemMediaControls =
            std::make_unique<Integrations::Smtc>(mediaControls.get(), mainWindowInstance.get());
#endif
    }

    connect(qApp, &QApplication::aboutToQuit, this, [this] {
        saveState();
        corePlayer.stop();
    });
}

Application::~Application() = default;

QList<Ui::SkinnedWindow*> Application::windows() const {
    QList<Ui::SkinnedWindow*> out{
        mainWindowInstance.get(), equalizerWindowInstance.get(), playlistWindowInstance.get(),
        nowPlayingWindowInstance.get()
    };
    if (milkdropWindowInstance) {
        out << milkdropWindowInstance.get();
    }
    return out;
}

void Application::layoutWindows() {
    // Default Winamp stack: main, equalizer below it, playlist below that.
    const QPoint mainPos =
        settings.value(QStringLiteral("mainWindow/pos"), QPoint(100, 100)).toPoint();
    const QPoint eqDefault = mainPos + QPoint(0, mainWindowInstance->height());
    const QPoint plDefault = eqDefault + QPoint(0, equalizerWindowInstance->height());
    mainWindowInstance->placeAt(mainPos);
    equalizerWindowInstance->placeAt(
        settings.value(QStringLiteral("equalizer/pos"), eqDefault).toPoint()
    );
    playlistWindowInstance->placeAt(
        settings.value(QStringLiteral("playlist/pos"), plDefault).toPoint()
    );
    // "Now playing" defaults to the right of the main window, Milkdrop below it.
    nowPlayingWindowInstance->placeAt(settings
                                          .value(
                                              QStringLiteral("nowPlaying/pos"),
                                              mainPos + QPoint(mainWindowInstance->width(), 0)
                                          )
                                          .toPoint());
    if (milkdropWindowInstance) {
        milkdropWindowInstance->placeAt(
            settings
                .value(
                    QStringLiteral("milkdrop/pos"),
                    mainPos + QPoint(mainWindowInstance->width(), mainWindowInstance->height())
                )
                .toPoint()
        );
    }
}

void Application::start() {
    mainWindowInstance->show();
    if (settings.value(QStringLiteral("equalizer/visible"), true).toBool()) {
        equalizerWindowInstance->show();
    }
    if (settings.value(QStringLiteral("playlist/visible"), true).toBool()) {
        playlistWindowInstance->show();
    }
    if (settings.value(QStringLiteral("nowPlaying/visible"), false).toBool()) {
        nowPlayingWindowInstance->show();
    }
    if (milkdropWindowInstance
        && settings.value(QStringLiteral("milkdrop/visible"), false).toBool()) {
        milkdropWindowInstance->show();
    }
    layoutWindows();
    mainWindowInstance->setEqButton(equalizerWindowInstance->isVisible());
    mainWindowInstance->setPlButton(playlistWindowInstance->isVisible());
    mainWindowInstance->activateWindow();

    if (startOptions.offline) {
        return;
    }
    const Yandex::TokenSource token = Yandex::FindToken(TokenFile(), YaampTokenFiles());
    if (token.token.isEmpty()) {
        mainWindowInstance->setStatusText(QStringLiteral("Войдите: правый клик → Войти"));
        QTimer::singleShot(0, this, &Application::login);
        return;
    }
    qInfo("Using Yandex token from %s", qPrintable(token.origin));
    // A token imported from the old Yaamp gets copied into our own config.
    const bool imported = !token.origin.startsWith(App::ConfigDir())
        && !token.origin.startsWith(QLatin1String("environment"));
    applyToken(token.token, imported);
}

void Application::applyToken(const QString& token, bool save) {
    apiClient.setToken(token);
    mainWindowInstance->setStatusText(QStringLiteral("Подключаюсь к Яндекс Музыке..."));
    yandexLibrary.connectAccount([this, token,
                                  save](const Yandex::Account& acc, const QString& error) {
        if (!error.isEmpty()) {
            mainWindowInstance->setStatusText(QStringLiteral("Вход не удался: ") + error);
            return;
        }
        if (save) {
            Yandex::SaveToken(TokenFile(), token);
        }
        mainWindowInstance->setStatusText(QStringLiteral("Привет, %1!").arg(acc.displayName));
        if (corePlayer.playlist().isEmpty()) {
            Ui::PlayLikes(&corePlayer, false);
        }
    });
}

void Application::login() {
    Ui::LoginDialog dlg(&networkManager, mainWindowInstance.get());
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    applyToken(dlg.token(), true);
}

void Application::logout() {
    corePlayer.clearQueue();
    yandexLibrary.logout();
    Yandex::ForgetToken(TokenFile());
    mainWindowInstance->setStatusText(QStringLiteral("Вы вышли из аккаунта"));
}

bool Application::loadSkin(const QString& path) {
    std::unique_ptr<Skins::Skin> skin;
    try {
        skin = std::make_unique<Skins::Skin>(Skins::Skin::LoadFile(path, &baseSkin));
    } catch (const Skins::Error& error) {
        mainWindowInstance->setStatusText(
            QStringLiteral("Не удалось загрузить скин: ") + QString::fromUtf8(error.what())
        );
        return false;
    }
    // Swap after the windows point at the new skin.
    for (Ui::SkinnedWindow* window : windows()) {
        window->setSkin(skin.get());
    }
    currentSkin = std::move(skin);
    settings.setValue(QStringLiteral("skin"), path);
    return true;
}

void Application::setScale(double scale, bool persist) {
    const double old = mainWindowInstance->scale();
    // Everything docked to the main window — hidden windows too, so they are
    // still in place when shown — in order of distance from the main window.
    const QList<Ui::SkinnedWindow*> all = windows();
    QList<QRect> rects;
    for (Ui::SkinnedWindow* window : all) {
        rects << window->frameGeometry();
    }
    QList<std::pair<Ui::SkinnedWindow*, QPoint>> docked;  // offsets in skin pixels
    for (int i : Ui::ConnectedGroup(0, rects)) {
        const QPointF off = QPointF(all[i]->pos() - mainWindowInstance->pos()) / old;
        docked.append({all[i], QPoint(qRound(off.x()), qRound(off.y()))});
    }

    for (Ui::SkinnedWindow* window : all) {
        window->setScale(scale);
    }
    const double s = mainWindowInstance->scale();
    const QPoint mainPos = mainWindowInstance->pos();  // setScale may have moved it back on screen

    // Re-stack. Window sizes are rounded individually, so snap each window to
    // the ones already placed to close 1 px rounding gaps.
    QList<QRect> placed{mainWindowInstance->frameGeometry()};
    for (const auto& [w, off] : docked) {
        QRect rect(mainPos + QPoint(qRound(off.x() * s), qRound(off.y() * s)), w->size());
        rect.moveTopLeft(Ui::SnapToOthers(rect, placed, 4));
        w->move(rect.topLeft());
        placed << rect;
    }
    // Keep the visible group on screen as a whole...
    QRect bounds = mainWindowInstance->frameGeometry();
    for (const auto& [w, off] : docked) {
        if (w->isVisible()) {
            bounds |= w->frameGeometry();
        }
    }
    QList<QRect> screens;
    for (QScreen* sc : QGuiApplication::screens()) {
        screens << sc->availableGeometry();
    }
    const QRect screen = Ui::PickScreen(bounds, screens);
    const QPoint shift = Ui::ClampInside(bounds, screen) - bounds.topLeft();
    if (!shift.isNull()) {
        mainWindowInstance->move(mainWindowInstance->pos() + shift);
        for (const auto& [w, off] : docked) {
            w->move(w->pos() + shift);
        }
    }
    // ...and if it's taller/wider than the screen, every window must still be reachable.
    if (!screen.isEmpty()
        && (bounds.width() > screen.width() || bounds.height() > screen.height())) {
        for (Ui::SkinnedWindow* window : all) {
            window->ensureVisible();
        }
    }

    transientScale = !persist;
    if (persist) {
        settings.setValue(QStringLiteral("scale"), s);
        saveState();
    }
}

void Application::setAlwaysOnTop(bool on) {
    for (Ui::SkinnedWindow* window : windows()) {
        const bool visible = window->isVisible();
        window->setWindowFlag(Qt::WindowStaysOnTopHint, on);
        if (visible) {
            window->show();  // changing flags hides the window
        }
    }
    settings.setValue(QStringLiteral("alwaysOnTop"), on);
}

void Application::setEqualizerVisible(bool on) {
    equalizerWindowInstance->setVisible(on);
    if (on) {
        equalizerWindowInstance->ensureVisible();
    }
    mainWindowInstance->setEqButton(on);
    settings.setValue(QStringLiteral("equalizer/visible"), on);
}

void Application::setPlaylistVisible(bool on) {
    playlistWindowInstance->setVisible(on);
    if (on) {
        playlistWindowInstance->ensureVisible();
    }
    mainWindowInstance->setPlButton(on);
    settings.setValue(QStringLiteral("playlist/visible"), on);
}

void Application::setMilkdropVisible(bool on) {
    if (!milkdropWindowInstance) {
        return;
    }
    milkdropWindowInstance->setVisible(on);
    if (on) {
        milkdropWindowInstance->ensureVisible();
    }
    settings.setValue(QStringLiteral("milkdrop/visible"), on);
}

void Application::setNowPlayingVisible(bool on) {
    nowPlayingWindowInstance->setVisible(on);
    if (on) {
        nowPlayingWindowInstance->ensureVisible();
    }
    settings.setValue(QStringLiteral("nowPlaying/visible"), on);
}

void Application::quit() {
    if (quitting) {
        return;
    }
    quitting = true;
    saveState();
    corePlayer.shutDown();  // sends the wave "skip" for the track in progress
    for (Ui::SkinnedWindow* window : windows()) {
        window->hide();
    }
    // Queued, so a quit() from inside a nested loop (a menu) lands in the main one.
    const auto finish = [] {
        QMetaObject::invokeMethod(qApp, &QApplication::quit, Qt::QueuedConnection);
    };
    if (apiClient.pendingPosts() == 0) {
        return finish();
    }
    connect(&apiClient, &Yandex::ApiClient::postsSettled, this, finish);
    QTimer::singleShot(1500, this, finish);
}

void Application::saveState() {
    settings.setValue(QStringLiteral("volume"), mainWindowInstance->volume());
    settings.setValue(QStringLiteral("balance"), mainWindowInstance->balance());
    settings.setValue(QStringLiteral("vis/mode"), int(mainWindowInstance->visMode()));
    settings.setValue(QStringLiteral("time/remaining"), mainWindowInstance->showsRemainingTime());
    settings.setValue(QStringLiteral("equalizer/auto"), equalizerWindowInstance->autoOn());
    if (!Ui::SkinnedWindow::CanPositionWindows() || !mainWindowInstance->isVisible()
        || transientScale) {
        return;
    }
    settings.setValue(QStringLiteral("mainWindow/pos"), mainWindowInstance->pos());
    settings.setValue(QStringLiteral("equalizer/pos"), equalizerWindowInstance->pos());
    settings.setValue(QStringLiteral("playlist/pos"), playlistWindowInstance->pos());
    settings.setValue(QStringLiteral("nowPlaying/pos"), nowPlayingWindowInstance->pos());
    if (milkdropWindowInstance) {
        settings.setValue(QStringLiteral("milkdrop/pos"), milkdropWindowInstance->pos());
    }
}

// ------------------------------------------------------------------ menus & keys

void Application::installShortcuts(QWidget* widget) {
    auto add = [widget](const QKeySequence& key, auto fn) {
        auto* a = new QAction(widget);
        a->setShortcut(key);
        QObject::connect(a, &QAction::triggered, widget, fn);
        widget->addAction(a);
    };
    // Winamp's classic keys.
    for (int key :
         {Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V, Qt::Key_B, Qt::Key_Left, Qt::Key_Right}) {
        add(QKeySequence(key), [this, key] { transportKey(key); });
    }
    add(QKeySequence(Qt::ALT | Qt::Key_G),
        [this] { setEqualizerVisible(!equalizerWindowInstance->isVisible()); });
    add(QKeySequence(Qt::ALT | Qt::Key_E),
        [this] { setPlaylistVisible(!playlistWindowInstance->isVisible()); });
    add(QKeySequence(Qt::CTRL | Qt::Key_D),
        [this] { setScale(std::abs(mainWindowInstance->scale() - 2.0) < 1e-6 ? 1.0 : 2.0); });
    add(QKeySequence(Qt::CTRL | Qt::Key_W),
        [this] { mainWindowInstance->setShaded(!mainWindowInstance->isShaded()); });
    add(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K), [this] {
        setMilkdropVisible(milkdropWindowInstance && !milkdropWindowInstance->isVisible());
    });
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
    QAction* eq = menu->addAction(QStringLiteral("Эквалайзер"), this, [this](bool on) {
        setEqualizerVisible(on);
    });
    eq->setCheckable(true);
    eq->setChecked(equalizerWindowInstance->isVisible());
    eq->setShortcut(QKeySequence(Qt::ALT | Qt::Key_G));
    QAction* pl = menu->addAction(QStringLiteral("Плейлист"), this, [this](bool on) {
        setPlaylistVisible(on);
    });
    pl->setCheckable(true);
    pl->setChecked(playlistWindowInstance->isVisible());
    pl->setShortcut(QKeySequence(Qt::ALT | Qt::Key_E));
    QAction* np = menu->addAction(QStringLiteral("Сейчас играет"), this, [this](bool on) {
        setNowPlayingVisible(on);
    });
    np->setCheckable(true);
    np->setChecked(nowPlayingWindowInstance->isVisible());
    if (milkdropWindowInstance) {
        QAction* md = menu->addAction(QStringLiteral("Milkdrop"), this, [this](bool on) {
            setMilkdropVisible(on);
        });
        md->setCheckable(true);
        md->setChecked(milkdropWindowInstance->isVisible());
        md->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K));
    }

    QMenu* vis = menu->addMenu(QStringLiteral("Визуализация"));
    auto* visGroup = new QActionGroup(vis);
    const std::pair<Ui::MainWindow::VisMode, QString> modes[] = {
        {Ui::MainWindow::VisMode::Spectrum, QStringLiteral("Спектр")},
        {Ui::MainWindow::VisMode::Oscilloscope, QStringLiteral("Осциллограф")},
        {Ui::MainWindow::VisMode::Off, QStringLiteral("Выключена")}
    };
    for (const auto& [mode, name] : modes) {
        QAction* action = vis->addAction(name, this, [this, mode] {
            mainWindowInstance->setVisMode(mode);
            saveState();
        });
        action->setCheckable(true);
        action->setChecked(mainWindowInstance->visMode() == mode);
        visGroup->addAction(action);
    }

    QMenu* skins = menu->addMenu(QStringLiteral("Скины"));
    const QStringList builtin = QDir(QStringLiteral(":/skins"))
                                    .entryList({QStringLiteral("*.wsz")}, QDir::Files, QDir::Name);
    for (const QString& name : builtin) {
        const QString path = QStringLiteral(":/skins/") + name;
        skins->addAction(QString(name).chopped(4), this, [this, path] { loadSkin(path); });
    }
    skins->addSeparator();
    skins->addAction(QStringLiteral("Загрузить скин..."), this, [this] {
        const QString f = QFileDialog::getOpenFileName(
            mainWindowInstance.get(), QStringLiteral("Скин Winamp"), QDir::homePath(),
            QStringLiteral("Скины Winamp (*.wsz *.zip)")
        );
        if (!f.isEmpty()) {
            loadSkin(f);
        }
    });

    QMenu* size = menu->addMenu(QStringLiteral("Размер"));
    auto* sizes = new QActionGroup(size);
    for (double s : {1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0}) {
        QAction* action =
            size->addAction(QStringLiteral("%1%").arg(qRound(s * 100)), this, [this, s] {
                setScale(s);
            });
        action->setCheckable(true);
        action->setChecked(std::abs(mainWindowInstance->scale() - s) < 1e-6);
        sizes->addAction(action);
    }
    size->addSeparator();
    QAction* dbl = size->addAction(QStringLiteral("Двойной размер"), this, [this] {
        setScale(std::abs(mainWindowInstance->scale() - 2.0) < 1e-6 ? 1.0 : 2.0);
    });
    dbl->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));

    QAction* top = menu->addAction(QStringLiteral("Поверх всех окон"), this, [this](bool on) {
        setAlwaysOnTop(on);
    });
    top->setCheckable(true);
    top->setChecked(mainWindowInstance->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
}

void Application::showSourcesMenu(QPoint globalPos) {
    auto* menu = new QMenu(mainWindowInstance.get());
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::AddLibraryActions(menu, &corePlayer, mainWindowInstance.get(), [this] { login(); });
    menu->popup(globalPos);
}

void Application::showMainMenu(QPoint globalPos) {
    auto* menu = new QMenu(mainWindowInstance.get());
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::AddLibraryActions(menu, &corePlayer, mainWindowInstance.get(), [this] { login(); });
    menu->addSeparator();
    fillWindowActions(menu);
    menu->addSeparator();
    if (yandexLibrary.isLoggedIn()) {
        menu->addAction(
            QStringLiteral("Выйти из аккаунта (%1)").arg(yandexLibrary.account().displayName), this,
            &Application::logout
        );
    }
    menu->addAction(QStringLiteral("Закрыть QiYaa"), this, &Application::quit);
    menu->popup(globalPos);
}

QImage Application::snapshot() const {
    QRect bounds;
    for (Ui::SkinnedWindow* window : windows()) {
        if (window->isVisible()) {
            bounds |= window->frameGeometry();
        }
    }
    QImage image(bounds.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    for (Ui::SkinnedWindow* window : windows()) {
        if (window->isVisible()) {
            painter.drawPixmap(window->pos() - bounds.topLeft(), window->grab());
        }
    }
    return image;
}

}  // namespace App
