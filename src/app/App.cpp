#include "app/App.h"

#include <algorithm>
#include <cmath>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QMenu>
#include <QPainter>
#include <QScreen>
#include <QTimer>

#include "app/Paths.h"
#include "audio/Equalizer.h"
#include "core/CoverCache.h"
#include "integrations/MediaControls.h"
#ifdef QIYAA_HAVE_MPRIS
#include "integrations/Mpris.h"
#endif
#ifdef QIYAA_HAVE_SMTC
#include "integrations/Smtc.h"
#endif
#include "ui/EqualizerWindow.h"
#include "ui/GenWindow.h"
#include "ui/MilkdropWindow.h"  // stays null without QIYAA_HAVE_MILKDROP
#include "ui/NowPlayingWindow.h"
#include "ui/LibraryMenu.h"
#include "ui/LoginDialog.h"
#include "ui/MainWindow.h"
#include "ui/PlaylistWindow.h"
#include "ui/Snap.h"
#include "yandex/Token.h"

namespace qiyaa {

using audio::EqSettings;

namespace {

QString settingsPath(const App::Options& o, QTemporaryDir* tmp) {
    if (tmp) return tmp->filePath(QStringLiteral("settings.ini"));
    return o.settingsFile.isEmpty() ? paths::configDir() + QStringLiteral("/settings.ini") : o.settingsFile;
}

EqSettings readEq(const QSettings& s) {
    EqSettings eq;
    eq.enabled = s.value(QStringLiteral("equalizer/enabled"), true).toBool();
    eq.preampDb = s.value(QStringLiteral("equalizer/preamp"), 0.0).toDouble();
    const QStringList bands = s.value(QStringLiteral("equalizer/bands")).toStringList();
    for (int i = 0; i < audio::kEqBands && i < bands.size(); ++i) eq.bandsDb[i] = bands[i].toDouble();
    return eq;
}

void writeEq(QSettings& s, const EqSettings& eq) {
    s.setValue(QStringLiteral("equalizer/enabled"), eq.enabled);
    s.setValue(QStringLiteral("equalizer/preamp"), eq.preampDb);
    QStringList bands;
    for (double b : eq.bandsDb) bands << QString::number(b, 'f', 1);
    s.setValue(QStringLiteral("equalizer/bands"), bands);
}

}  // namespace

App::App(const Options& options, QObject* parent)
    : QObject(parent),
      m_options(options),
      m_tmpDir(options.readOnlySettings ? std::make_unique<QTemporaryDir>() : nullptr),
      m_settings(settingsPath(options, m_tmpDir.get()), QSettings::IniFormat),
      m_baseSkin(Skin::builtinBase()),
      m_skin(std::make_unique<Skin>(m_baseSkin)),
      m_api(&m_nam),
      m_library(&m_api),
      m_player(&m_library, &m_engine) {
    if (m_options.audio) {
        QString err;
        if (!m_engine.init(&err)) qWarning("Audio: %s", qPrintable(err));
        else qInfo("Audio backend: %s", qPrintable(m_engine.backendName()));
    }

    const QString skinPath = options.skinOverride.isEmpty() ? m_settings.value(QStringLiteral("skin")).toString() : options.skinOverride;
    if (!skinPath.isEmpty()) {
        auto s = std::make_unique<Skin>();
        if (s->loadFromFile(skinPath, &m_baseSkin)) m_skin = std::move(s);
    }

    m_main = std::make_unique<MainWindow>(&m_player, m_skin.get());
    m_eq = std::make_unique<EqualizerWindow>(m_skin.get());
    m_pl = std::make_unique<PlaylistWindow>(&m_player, m_skin.get());
    m_covers = std::make_unique<CoverCache>(&m_nam, m_tmpDir ? m_tmpDir->filePath(QStringLiteral("covers")) : QString());
    m_np = std::make_unique<NowPlayingWindow>(&m_player, m_covers.get(), m_skin.get());
    for (QWidget* w : std::initializer_list<QWidget*>{m_main.get(), m_eq.get(), m_pl.get(), m_np.get()}) installShortcuts(w);
    m_eq->setSecondary();
    m_pl->setSecondary();
    m_np->setSecondary();
    m_np->setSizeSteps(m_settings.value(QStringLiteral("nowPlaying/steps"), QSize(0, 0)).toSize());
    connect(m_np.get(), &NowPlayingWindow::closeRequested, this, [this] { setNowPlayingVisible(false); });
    connect(m_np.get(), &GenWindow::sizeStepsChanged, this, [this](QSize s) { m_settings.setValue(QStringLiteral("nowPlaying/steps"), s); });
#if defined(QIYAA_HAVE_MILKDROP)
    {
        const QString userPresets = (m_tmpDir ? m_tmpDir->path() : paths::configDir()) + QStringLiteral("/milkdrop");
        m_md = std::make_unique<MilkdropWindow>(&m_engine, QStringLiteral(":/milkdrop"), userPresets, m_skin.get());
        installShortcuts(m_md.get());
        m_md->setSecondary();
        m_md->setSizeSteps(m_settings.value(QStringLiteral("milkdrop/steps"), QSize(0, 4)).toSize());
        m_md->setShuffle(m_settings.value(QStringLiteral("milkdrop/shuffle"), true).toBool());
        m_md->setLocked(m_settings.value(QStringLiteral("milkdrop/locked"), false).toBool());
        m_md->setPresetSeconds(m_settings.value(QStringLiteral("milkdrop/seconds"), 30).toInt());
        m_md->setBlackPresets(m_settings.value(QStringLiteral("milkdrop/black")).toStringList());
        m_md->selectPreset(m_settings.value(QStringLiteral("milkdrop/preset")).toString());
        connect(m_md.get(), &MilkdropWindow::closeRequested, this, [this] { setMilkdropVisible(false); });
        connect(m_md.get(), &GenWindow::sizeStepsChanged, this, [this](QSize s) { m_settings.setValue(QStringLiteral("milkdrop/steps"), s); });
        connect(m_md.get(), &MilkdropWindow::settingsChanged, this, [this] {
            m_settings.setValue(QStringLiteral("milkdrop/shuffle"), m_md->shuffle());
            m_settings.setValue(QStringLiteral("milkdrop/locked"), m_md->locked());
            m_settings.setValue(QStringLiteral("milkdrop/seconds"), m_md->presetSeconds());
            m_settings.setValue(QStringLiteral("milkdrop/preset"), m_md->currentPreset());
            m_settings.setValue(QStringLiteral("milkdrop/black"), m_md->blackPresets());
        });
        // Picked by hand: say which one it is (like Milkdrop's own title display).
        connect(m_md.get(), &MilkdropWindow::presetChanged, this, [this](const QString& name, bool byUser) {
            if (byUser) m_main->setStatusText(QStringLiteral("Milkdrop: ") + name);
        });
        connect(m_md.get(), &MilkdropWindow::transportKey, this, &App::transportKey);
        connect(&m_engine, &audio::AudioEngine::stateChanged, m_md.get(),
                [this](audio::AudioEngine::State s) { m_md->setPlaying(s == audio::AudioEngine::State::Playing); });
    }
#endif

    // Main window.
    m_main->setVolume(m_settings.value(QStringLiteral("volume"), 75).toInt());
    m_main->setBalance(m_settings.value(QStringLiteral("balance"), 0).toInt());
    m_main->setVisMode(MainWindow::VisMode(std::clamp(m_settings.value(QStringLiteral("vis/mode"), 0).toInt(), 0, 2)));
    m_main->setShowsRemainingTime(m_settings.value(QStringLiteral("time/remaining"), false).toBool());
    connect(m_main.get(), &MainWindow::eqToggleRequested, this, [this] { setEqualizerVisible(!m_eq->isVisible()); });
    connect(m_main.get(), &MainWindow::plToggleRequested, this, [this] { setPlaylistVisible(!m_pl->isVisible()); });
    connect(m_main.get(), &MainWindow::menuRequested, this, &App::showMainMenu);
    connect(m_main.get(), &MainWindow::sourcesMenuRequested, this, &App::showSourcesMenu);
    connect(m_main.get(), &MainWindow::closeRequested, this, &App::quit);
    // Minimising the main window takes the equalizer and playlist with it.
    connect(m_main.get(), &MainWindow::minimizedChanged, this, [this](bool minimized) {
        if (minimized) {
            m_eq->hide();
            m_pl->hide();
            m_np->hide();
            if (m_md) m_md->hide();
        } else {
            if (m_settings.value(QStringLiteral("equalizer/visible"), true).toBool()) m_eq->show();
            if (m_settings.value(QStringLiteral("playlist/visible"), true).toBool()) m_pl->show();
            if (m_settings.value(QStringLiteral("nowPlaying/visible"), false).toBool()) m_np->show();
            if (m_md && m_settings.value(QStringLiteral("milkdrop/visible"), false).toBool()) m_md->show();
        }
    });

    // Equalizer.
    const EqSettings eq = readEq(m_settings);
    m_eq->setSettings(eq);
    m_eq->setAutoOn(m_settings.value(QStringLiteral("equalizer/auto"), false).toBool());
    m_engine.setEqualizer(eq);
    connect(m_eq.get(), &EqualizerWindow::settingsChanged, this, [this](const EqSettings& s) {
        m_engine.setEqualizer(s);
        writeEq(m_settings, s);
    });
    connect(m_eq.get(), &EqualizerWindow::statusText, m_main.get(), &MainWindow::setStatusText);
    // The EQ's shade mode shows/controls the main window's volume and balance.
    m_eq->setMixer(m_main->volume(), m_main->balance());
    connect(m_main.get(), &MainWindow::volumeChanged, this, [this](int v) { m_eq->setMixer(v, m_main->balance()); });
    connect(m_main.get(), &MainWindow::balanceChanged, this, [this](int b) { m_eq->setMixer(m_main->volume(), b); });
    connect(m_eq.get(), &EqualizerWindow::volumeRequested, m_main.get(), &MainWindow::setVolume);
    connect(m_eq.get(), &EqualizerWindow::balanceRequested, m_main.get(), &MainWindow::setBalance);
    connect(m_eq.get(), &EqualizerWindow::closeRequested, this, [this] { setEqualizerVisible(false); });

    // Playlist.
    m_pl->setSizeSteps(m_settings.value(QStringLiteral("playlist/steps"), QSize(0, 4)).toSize());
    connect(m_pl.get(), &PlaylistWindow::closeRequested, this, [this] { setPlaylistVisible(false); });
    connect(m_pl.get(), &PlaylistWindow::sourcesMenuRequested, this, &App::showMainMenu);
    connect(m_pl.get(), &PlaylistWindow::sizeStepsChanged, this,
            [this](QSize s) { m_settings.setValue(QStringLiteral("playlist/steps"), s); });

    for (SkinnedWindow* w : windows())
        connect(w, &SkinnedWindow::moveFinished, this, &App::saveState);

    // Shade states: applied before the windows are placed, so saved positions match.
    const std::pair<SkinnedWindow*, QString> shades[] = {{m_main.get(), QStringLiteral("mainWindow/shaded")},
                                                         {m_eq.get(), QStringLiteral("equalizer/shaded")},
                                                         {m_pl.get(), QStringLiteral("playlist/shaded")}};
    for (const auto& [w, key] : shades) {
        w->setShaded(m_settings.value(key, false).toBool());
        connect(w, &SkinnedWindow::shadeChanged, this, [this, key](bool on) {
            m_settings.setValue(key, on);
            saveState();
        });
    }

    const double scale = m_settings.value(QStringLiteral("scale"), 1.0).toDouble();
    for (SkinnedWindow* w : windows()) w->setScale(scale);
    if (m_settings.value(QStringLiteral("alwaysOnTop"), false).toBool()) setAlwaysOnTop(true);

    if (m_options.mediaIntegration) {
        MediaControls::Hooks hooks;
        hooks.volume = [this] { return m_main->volume(); };
        hooks.setVolume = [this](int v) { m_main->setVolume(v); };
        hooks.raise = [this] {
            if (m_main->isMinimized()) m_main->showNormal();
            for (SkinnedWindow* w : windows())
                if (w->isVisible()) w->raise();
            m_main->activateWindow();
        };
        hooks.quit = [this] { quit(); };
        m_mediaControls = std::make_unique<MediaControls>(&m_player, m_covers.get(), hooks);
        connect(m_main.get(), &MainWindow::volumeChanged, m_mediaControls.get(), &MediaControls::volumeChanged);
#if defined(QIYAA_HAVE_MPRIS)
        m_osMedia = std::make_unique<Mpris>(m_mediaControls.get());
#elif defined(QIYAA_HAVE_SMTC)
        m_osMedia = std::make_unique<Smtc>(m_mediaControls.get(), m_main.get());
#endif
    }

    connect(qApp, &QApplication::aboutToQuit, this, [this] {
        saveState();
        m_player.stop();
    });
}

App::~App() = default;

QList<SkinnedWindow*> App::windows() const {
    QList<SkinnedWindow*> out{m_main.get(), m_eq.get(), m_pl.get(), m_np.get()};
    if (m_md) out << m_md.get();
    return out;
}

void App::layoutWindows() {
    // Default Winamp stack: main, equalizer below it, playlist below that.
    const QPoint mainPos = m_settings.value(QStringLiteral("mainWindow/pos"), QPoint(100, 100)).toPoint();
    const QPoint eqDefault = mainPos + QPoint(0, m_main->height());
    const QPoint plDefault = eqDefault + QPoint(0, m_eq->height());
    m_main->placeAt(mainPos);
    m_eq->placeAt(m_settings.value(QStringLiteral("equalizer/pos"), eqDefault).toPoint());
    m_pl->placeAt(m_settings.value(QStringLiteral("playlist/pos"), plDefault).toPoint());
    // "Now playing" defaults to the right of the main window, Milkdrop below it.
    m_np->placeAt(m_settings.value(QStringLiteral("nowPlaying/pos"), mainPos + QPoint(m_main->width(), 0)).toPoint());
    if (m_md)
        m_md->placeAt(m_settings.value(QStringLiteral("milkdrop/pos"), mainPos + QPoint(m_main->width(), m_main->height())).toPoint());
}

void App::start() {
    m_main->show();
    if (m_settings.value(QStringLiteral("equalizer/visible"), true).toBool()) m_eq->show();
    if (m_settings.value(QStringLiteral("playlist/visible"), true).toBool()) m_pl->show();
    if (m_settings.value(QStringLiteral("nowPlaying/visible"), false).toBool()) m_np->show();
    if (m_md && m_settings.value(QStringLiteral("milkdrop/visible"), false).toBool()) m_md->show();
    layoutWindows();
    m_main->setEqButton(m_eq->isVisible());
    m_main->setPlButton(m_pl->isVisible());
    m_main->activateWindow();

    if (m_options.offline) return;
    const yandex::TokenSource token = yandex::findToken();
    if (token.token.isEmpty()) {
        m_main->setStatusText(QStringLiteral("Войдите: правый клик → Войти"));
        QTimer::singleShot(0, this, &App::login);
        return;
    }
    qInfo("Using Yandex token from %s", qPrintable(token.origin));
    // A token imported from the old Yaamp gets copied into our own config.
    const bool imported = !token.origin.startsWith(paths::configDir()) && !token.origin.startsWith(QLatin1String("environment"));
    applyToken(token.token, imported);
}

void App::applyToken(const QString& token, bool save) {
    m_api.setToken(token);
    m_main->setStatusText(QStringLiteral("Подключаюсь к Яндекс Музыке..."));
    m_library.connectAccount([this, token, save](const yandex::Account& acc, const QString& err) {
        if (!err.isEmpty()) {
            m_main->setStatusText(QStringLiteral("Вход не удался: ") + err);
            return;
        }
        if (save) yandex::saveToken(token);
        m_main->setStatusText(QStringLiteral("Привет, %1!").arg(acc.displayName));
        if (m_player.playlist().isEmpty()) sources::playLikes(&m_player, false);
    });
}

void App::login() {
    LoginDialog dlg(&m_nam, m_main.get());
    if (dlg.exec() != QDialog::Accepted) return;
    applyToken(dlg.token(), true);
}

void App::logout() {
    m_player.clearQueue();
    m_library.logout();
    yandex::forgetToken();
    m_main->setStatusText(QStringLiteral("Вы вышли из аккаунта"));
}

bool App::loadSkin(const QString& path) {
    auto s = std::make_unique<Skin>();
    QString err;
    if (!s->loadFromFile(path, &m_baseSkin, &err)) {
        m_main->setStatusText(QStringLiteral("Не удалось загрузить скин: ") + err);
        return false;
    }
    // Swap after the windows point at the new skin.
    for (SkinnedWindow* w : windows()) w->setSkin(s.get());
    m_skin = std::move(s);
    m_settings.setValue(QStringLiteral("skin"), path);
    return true;
}

void App::setScale(double scale, bool persist) {
    const double old = m_main->scale();
    // Everything docked to the main window — hidden windows too, so they are
    // still in place when shown — in order of distance from the main window.
    const QList<SkinnedWindow*> all = windows();
    QList<QRect> rects;
    for (SkinnedWindow* w : all) rects << w->frameGeometry();
    QList<std::pair<SkinnedWindow*, QPoint>> docked;  // offsets in skin pixels
    for (int i : snap::connectedGroup(0, rects)) {
        const QPointF off = QPointF(all[i]->pos() - m_main->pos()) / old;
        docked.append({all[i], QPoint(qRound(off.x()), qRound(off.y()))});
    }

    for (SkinnedWindow* w : all) w->setScale(scale);
    const double s = m_main->scale();
    const QPoint mainPos = m_main->pos();  // setScale may have moved it back on screen

    // Re-stack. Window sizes are rounded individually, so snap each window to
    // the ones already placed to close 1 px rounding gaps.
    QList<QRect> placed{m_main->frameGeometry()};
    for (const auto& [w, off] : docked) {
        QRect r(mainPos + QPoint(qRound(off.x() * s), qRound(off.y() * s)), w->size());
        r.moveTopLeft(snap::snapToOthers(r, placed, 4));
        w->move(r.topLeft());
        placed << r;
    }
    // Keep the visible group on screen as a whole...
    QRect bounds = m_main->frameGeometry();
    for (const auto& [w, off] : docked)
        if (w->isVisible()) bounds |= w->frameGeometry();
    QList<QRect> screens;
    for (QScreen* sc : QGuiApplication::screens()) screens << sc->availableGeometry();
    const QRect screen = snap::pickScreen(bounds, screens);
    const QPoint shift = snap::clampInside(bounds, screen) - bounds.topLeft();
    if (!shift.isNull()) {
        m_main->move(m_main->pos() + shift);
        for (const auto& [w, off] : docked) w->move(w->pos() + shift);
    }
    // ...and if it's taller/wider than the screen, every window must still be reachable.
    if (!screen.isEmpty() && (bounds.width() > screen.width() || bounds.height() > screen.height()))
        for (SkinnedWindow* w : all) w->ensureVisible();

    m_transientScale = !persist;
    if (persist) {
        m_settings.setValue(QStringLiteral("scale"), s);
        saveState();
    }
}

void App::setAlwaysOnTop(bool on) {
    for (SkinnedWindow* w : windows()) {
        const bool visible = w->isVisible();
        w->setWindowFlag(Qt::WindowStaysOnTopHint, on);
        if (visible) w->show();  // changing flags hides the window
    }
    m_settings.setValue(QStringLiteral("alwaysOnTop"), on);
}

void App::setEqualizerVisible(bool on) {
    m_eq->setVisible(on);
    if (on) m_eq->ensureVisible();
    m_main->setEqButton(on);
    m_settings.setValue(QStringLiteral("equalizer/visible"), on);
}

void App::setPlaylistVisible(bool on) {
    m_pl->setVisible(on);
    if (on) m_pl->ensureVisible();
    m_main->setPlButton(on);
    m_settings.setValue(QStringLiteral("playlist/visible"), on);
}

void App::setMilkdropVisible(bool on) {
    if (!m_md) return;
    m_md->setVisible(on);
    if (on) m_md->ensureVisible();
    m_settings.setValue(QStringLiteral("milkdrop/visible"), on);
}

void App::setNowPlayingVisible(bool on) {
    m_np->setVisible(on);
    if (on) m_np->ensureVisible();
    m_settings.setValue(QStringLiteral("nowPlaying/visible"), on);
}

void App::quit() {
    if (m_quitting) return;
    m_quitting = true;
    saveState();
    m_player.shutDown();  // sends the wave "skip" for the track in progress
    for (SkinnedWindow* w : windows()) w->hide();
    // Queued, so a quit() from inside a nested loop (a menu) lands in the main one.
    const auto finish = [] { QMetaObject::invokeMethod(qApp, &QApplication::quit, Qt::QueuedConnection); };
    if (m_api.pendingPosts() == 0) return finish();
    connect(&m_api, &yandex::ApiClient::postsSettled, this, finish);
    QTimer::singleShot(1500, this, finish);
}

void App::saveState() {
    m_settings.setValue(QStringLiteral("volume"), m_main->volume());
    m_settings.setValue(QStringLiteral("balance"), m_main->balance());
    m_settings.setValue(QStringLiteral("vis/mode"), int(m_main->visMode()));
    m_settings.setValue(QStringLiteral("time/remaining"), m_main->showsRemainingTime());
    m_settings.setValue(QStringLiteral("equalizer/auto"), m_eq->autoOn());
    if (!SkinnedWindow::canPositionWindows() || !m_main->isVisible() || m_transientScale) return;
    m_settings.setValue(QStringLiteral("mainWindow/pos"), m_main->pos());
    m_settings.setValue(QStringLiteral("equalizer/pos"), m_eq->pos());
    m_settings.setValue(QStringLiteral("playlist/pos"), m_pl->pos());
    m_settings.setValue(QStringLiteral("nowPlaying/pos"), m_np->pos());
    if (m_md) m_settings.setValue(QStringLiteral("milkdrop/pos"), m_md->pos());
}

// ------------------------------------------------------------------ menus & keys

void App::installShortcuts(QWidget* w) {
    auto add = [w](const QKeySequence& key, auto fn) {
        auto* a = new QAction(w);
        a->setShortcut(key);
        QObject::connect(a, &QAction::triggered, w, fn);
        w->addAction(a);
    };
    // Winamp's classic keys.
    for (int key : {Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V, Qt::Key_B, Qt::Key_Left, Qt::Key_Right})
        add(QKeySequence(key), [this, key] { transportKey(key); });
    add(QKeySequence(Qt::ALT | Qt::Key_G), [this] { setEqualizerVisible(!m_eq->isVisible()); });
    add(QKeySequence(Qt::ALT | Qt::Key_E), [this] { setPlaylistVisible(!m_pl->isVisible()); });
    add(QKeySequence(Qt::CTRL | Qt::Key_D), [this] { setScale(std::abs(m_main->scale() - 2.0) < 1e-6 ? 1.0 : 2.0); });
    add(QKeySequence(Qt::CTRL | Qt::Key_W), [this] { m_main->setShaded(!m_main->isShaded()); });
    add(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K), [this] { setMilkdropVisible(m_md && !m_md->isVisible()); });
}

void App::transportKey(int key) {
    switch (key) {
    case Qt::Key_Z: m_player.previous(); break;
    case Qt::Key_X: m_player.play(); break;
    case Qt::Key_C: m_player.pause(); break;
    case Qt::Key_V: m_player.stop(); break;
    case Qt::Key_B: m_player.next(); break;
    case Qt::Key_Left: m_player.seekTo(m_engine.positionSeconds() - 5); break;
    case Qt::Key_Right: m_player.seekTo(m_engine.positionSeconds() + 5); break;
    default: break;
    }
}

void App::fillWindowActions(QMenu* menu) {
    QAction* eq = menu->addAction(QStringLiteral("Эквалайзер"), this, [this](bool on) { setEqualizerVisible(on); });
    eq->setCheckable(true);
    eq->setChecked(m_eq->isVisible());
    eq->setShortcut(QKeySequence(Qt::ALT | Qt::Key_G));
    QAction* pl = menu->addAction(QStringLiteral("Плейлист"), this, [this](bool on) { setPlaylistVisible(on); });
    pl->setCheckable(true);
    pl->setChecked(m_pl->isVisible());
    pl->setShortcut(QKeySequence(Qt::ALT | Qt::Key_E));
    QAction* np = menu->addAction(QStringLiteral("Сейчас играет"), this, [this](bool on) { setNowPlayingVisible(on); });
    np->setCheckable(true);
    np->setChecked(m_np->isVisible());
    if (m_md) {
        QAction* md = menu->addAction(QStringLiteral("Milkdrop"), this, [this](bool on) { setMilkdropVisible(on); });
        md->setCheckable(true);
        md->setChecked(m_md->isVisible());
        md->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K));
    }

    QMenu* vis = menu->addMenu(QStringLiteral("Визуализация"));
    auto* visGroup = new QActionGroup(vis);
    const std::pair<MainWindow::VisMode, QString> modes[] = {{MainWindow::VisMode::Spectrum, QStringLiteral("Спектр")},
                                                             {MainWindow::VisMode::Oscilloscope, QStringLiteral("Осциллограф")},
                                                             {MainWindow::VisMode::Off, QStringLiteral("Выключена")}};
    for (const auto& [mode, name] : modes) {
        QAction* a = vis->addAction(name, this, [this, mode] { m_main->setVisMode(mode); saveState(); });
        a->setCheckable(true);
        a->setChecked(m_main->visMode() == mode);
        visGroup->addAction(a);
    }

    QMenu* skins = menu->addMenu(QStringLiteral("Скины"));
    const QStringList builtin = QDir(QStringLiteral(":/skins")).entryList({QStringLiteral("*.wsz")}, QDir::Files, QDir::Name);
    for (const QString& name : builtin) {
        const QString path = QStringLiteral(":/skins/") + name;
        skins->addAction(QString(name).chopped(4), this, [this, path] { loadSkin(path); });
    }
    skins->addSeparator();
    skins->addAction(QStringLiteral("Загрузить скин..."), this, [this] {
        const QString f = QFileDialog::getOpenFileName(m_main.get(), QStringLiteral("Скин Winamp"), QDir::homePath(),
                                                       QStringLiteral("Скины Winamp (*.wsz *.zip)"));
        if (!f.isEmpty()) loadSkin(f);
    });

    QMenu* size = menu->addMenu(QStringLiteral("Размер"));
    auto* sizes = new QActionGroup(size);
    for (double s : {1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0}) {
        QAction* a = size->addAction(QStringLiteral("%1%").arg(qRound(s * 100)), this, [this, s] { setScale(s); });
        a->setCheckable(true);
        a->setChecked(std::abs(m_main->scale() - s) < 1e-6);
        sizes->addAction(a);
    }
    size->addSeparator();
    QAction* dbl = size->addAction(QStringLiteral("Двойной размер"), this,
                                   [this] { setScale(std::abs(m_main->scale() - 2.0) < 1e-6 ? 1.0 : 2.0); });
    dbl->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));

    QAction* top = menu->addAction(QStringLiteral("Поверх всех окон"), this, [this](bool on) { setAlwaysOnTop(on); });
    top->setCheckable(true);
    top->setChecked(m_main->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
}

void App::showSourcesMenu(QPoint globalPos) {
    auto* menu = new QMenu(m_main.get());
    menu->setAttribute(Qt::WA_DeleteOnClose);
    addLibraryActions(menu, &m_player, m_main.get(), [this] { login(); });
    menu->popup(globalPos);
}

void App::showMainMenu(QPoint globalPos) {
    auto* menu = new QMenu(m_main.get());
    menu->setAttribute(Qt::WA_DeleteOnClose);
    addLibraryActions(menu, &m_player, m_main.get(), [this] { login(); });
    menu->addSeparator();
    fillWindowActions(menu);
    menu->addSeparator();
    if (m_library.isLoggedIn())
        menu->addAction(QStringLiteral("Выйти из аккаунта (%1)").arg(m_library.account().displayName), this, &App::logout);
    menu->addAction(QStringLiteral("Закрыть QiYaa"), this, &App::quit);
    menu->popup(globalPos);
}

QImage App::snapshot() const {
    QRect bounds;
    for (SkinnedWindow* w : windows())
        if (w->isVisible()) bounds |= w->frameGeometry();
    QImage img(bounds.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    for (SkinnedWindow* w : windows())
        if (w->isVisible()) p.drawPixmap(w->pos() - bounds.topLeft(), w->grab());
    return img;
}

}  // namespace qiyaa
