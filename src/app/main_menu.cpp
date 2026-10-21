#include "app/main_menu.h"

#include "app/account.h"
#include "app/jam_bridge.h"
#include "app/settings.h"
#include "app/telemetry_bridge.h"
#include "app/window_set.h"
#include "audio/audio_engine.h"
#include "core/lyrics_finder.h"
#include "core/player.h"
#include "sources/local/catalog.h"
#include "sources/yandex/catalog.h"
#include "sources/yandex/source.h"
#include "ui/library_menu.h"
#include "ui/main_window.h"
#include "ui/playlist_window.h"
#include "yandex/library.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLatin1String>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QTimer>

#include <cmath>
#include <utility>

namespace App {

namespace {

constexpr Qt::Key kTransportKeys[] =
    {Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V, Qt::Key_B, Qt::Key_Left, Qt::Key_Right};
constexpr QKeyCombination kDoubleSizeKey = Qt::CTRL | Qt::Key_D;
constexpr QKeyCombination kShadeMainKey = Qt::CTRL | Qt::Key_W;

double DoubledOrBack(double scale) {
    return std::abs(scale - 2.0) < 1e-6 ? 1.0 : 2.0;
}

}  // namespace

void TransportKey(Core::Player* player, Audio::AudioEngine* engine, int key) {
    switch (key) {
        case Qt::Key_Z: player->previous(); break;
        case Qt::Key_X: player->play(); break;
        case Qt::Key_C: player->pause(); break;
        case Qt::Key_V: player->stop(); break;
        case Qt::Key_B: player->next(); break;
        case Qt::Key_Left: player->seekTo(engine->positionSeconds() - 5); break;
        case Qt::Key_Right: player->seekTo(engine->positionSeconds() + 5); break;
        default: break;
    }
}

MainMenu::MainMenu(const Parts& menuParts, QObject* parent)
    : QObject(parent)
    , parts(menuParts) {
    for (Ui::SkinnedWindow* window : parts.windows->all()) {
        installShortcuts(window);
    }
    connect(parts.mainWindow, &Ui::MainWindow::menuRequested, this, [this](QPoint pos) {
        showMain(pos, parts.player->currentTrack());
    });
    connect(parts.mainWindow, &Ui::MainWindow::sourcesMenuRequested, this, &MainMenu::showSources);
    connect(parts.playlistWindow, &Ui::PlaylistWindow::sourcesMenuRequested, this, [this](QPoint pos) {
        showMain(pos, parts.player->currentTrack());
    });
    connect(parts.playlistWindow, &Ui::PlaylistWindow::trackMenuRequested, this, [this](QPoint pos, int row) {
        const auto& entries = parts.player->entries();
        showMain(pos, row >= 0 && row < entries.size() ? &entries[row].track : nullptr);
    });
}

void MainMenu::installShortcuts(QWidget* widget) {
    auto add = [this, widget](const QKeySequence& shortcut, auto handler) {
        auto* action = new QAction(widget);
        action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, handler);
        widget->addAction(action);
    };
    for (Qt::Key key : kTransportKeys) {
        add(QKeySequence(key), [this, key] { TransportKey(parts.player, parts.engine, key); });
    }
    for (Ui::SkinnedWindow* window : parts.windows->all()) {
        if (const QKeySequence& key = parts.windows->spec(window).key; !key.isEmpty()) {
            add(key, [this, window] { parts.windows->toggle(window); });
        }
    }
    add(QKeySequence(kDoubleSizeKey), [this] { parts.windows->setScale(DoubledOrBack(parts.mainWindow->scale())); });
    add(QKeySequence(kShadeMainKey), [this] { parts.mainWindow->setShaded(!parts.mainWindow->isShaded()); });
}

void MainMenu::showSources(QPoint globalPosition) {
    auto* menu = new QMenu(parts.mainWindow);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    addLocalActions(menu);
    Ui::AddLibraryActions(
        menu,
        parts.library,
        parts.catalog,
        parts.sources,
        parts.player->currentTrack(),
        parts.mainWindow,
        [this] { QTimer::singleShot(0, parts.account, &Account::login); }
    );
    menu->popup(globalPosition);
}

void MainMenu::showMain(QPoint globalPosition, const Core::Track* track) {
    auto* menu = new QMenu(parts.mainWindow);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    addLocalActions(menu);
    Ui::AddLibraryActions(menu, parts.library, parts.catalog, parts.sources, track, parts.mainWindow, [this] {
        QTimer::singleShot(0, parts.account, &Account::login);
    });
    menu->addSeparator();
    parts.jam->addMenu(menu);
    addWindowActions(menu);
    addModes(menu);
    addLooks(menu);
    menu->addSeparator();
    if (parts.library->isLoggedIn()) {
        const QString name = parts.library->account().displayName;
        menu->addAction(tr("Log out (%1)").arg(name), parts.account, &Account::logout);
    }
    menu->addAction(tr("Quit QiYaa"), this, &MainMenu::quitChosen);
    menu->popup(globalPosition);
}

void MainMenu::addLocalActions(QMenu* menu) {
    QAction* files = menu->addAction(tr("Open files…"), this, [this] {
        QTimer::singleShot(0, this, &MainMenu::openLocalFiles);
    });
    files->setObjectName(QStringLiteral("openLocalFiles"));
    QAction* folder = menu->addAction(tr("Open folder…"), this, [this] {
        QTimer::singleShot(0, this, &MainMenu::openLocalFolder);
    });
    folder->setObjectName(QStringLiteral("openLocalFolder"));
    menu->addSeparator();
}

void MainMenu::openLocalFiles() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        parts.mainWindow,
        tr("Open files"),
        localStartFolder(),
        tr("Audio files (*.mp3 *.flac *.wav *.m4a *.mp4 *.ogg *.oga)")
    );
    if (!paths.isEmpty()) {
        parts.settings->setLastLocalFolder(QFileInfo(paths.front()).absolutePath());
        parts.local->openFiles(paths);
    }
}

void MainMenu::openLocalFolder() {
    const QString path = QFileDialog::getExistingDirectory(parts.mainWindow, tr("Open folder"), localStartFolder());
    if (!path.isEmpty()) {
        parts.settings->setLastLocalFolder(path);
        parts.local->openFolder(path);
    }
}

void MainMenu::askForLrclib() {
    QMessageBox ask(
        QMessageBox::Question,
        tr("Lyrics from LRCLIB"),
        tr("LRCLIB is an open database of lyrics, most of them synced. For a track without synced lyrics of "
           "its own, it gets the track's title and first artist, and your computer's address. Turn it on?"),
        QMessageBox::NoButton,
        parts.mainWindow
    );
    QPushButton* turnOn = ask.addButton(tr("Turn on"), QMessageBox::AcceptRole);
    ask.setDefaultButton(ask.addButton(tr("Cancel"), QMessageBox::RejectRole));
    ask.exec();
    if (ask.clickedButton() == turnOn) {
        parts.settings->setLyricsThirdParty(true);
        parts.lyrics->setThirdPartyEnabled(true);
    }
}

void MainMenu::loadSkinFile() {
    const QString path = QFileDialog::getOpenFileName(
        parts.mainWindow, tr("Winamp skin"), QDir::homePath(), tr("Winamp skins (*.wsz *.zip)")
    );
    if (!path.isEmpty()) {
        Q_EMIT skinChosen(path);
    }
}

QString MainMenu::localStartFolder() const {
    const QString last = parts.settings->lastLocalFolder();
    return !last.isEmpty() && QFileInfo(last).isDir() ? last : QDir::homePath();
}

void MainMenu::addWindowActions(QMenu* menu) {
    for (Ui::SkinnedWindow* window : parts.windows->all()) {
        const WindowSet::Spec& spec = parts.windows->spec(window);
        if (!spec.menuText) {
            continue;
        }
        QAction* action =
            menu->addAction(QCoreApplication::translate("App::MainMenu", spec.menuText), this, [this, window](bool on) {
                parts.windows->setVisible(window, on);
            });
        action->setCheckable(true);
        action->setChecked(window->isVisible());
        action->setShortcut(spec.key);
    }
    if (parts.lyrics->hasThirdParty()) {
        QAction* lrclib = menu->addAction(tr("Lyrics from LRCLIB"), this, [this](bool on) {
            if (on) {
                return QTimer::singleShot(0, this, &MainMenu::askForLrclib);
            }
            parts.settings->setLyricsThirdParty(false);
            parts.lyrics->setThirdPartyEnabled(false);
        });
        lrclib->setObjectName(QStringLiteral("lyricsFromLrclib"));
        lrclib->setCheckable(true);
        lrclib->setChecked(parts.lyrics->thirdPartyEnabled());
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
            parts.mainWindow->setVisMode(mode);
            Q_EMIT visModeChosen();
        });
        action->setCheckable(true);
        action->setChecked(parts.mainWindow->visMode() == mode);
        visualizationGroup->addAction(action);
    }
}

void MainMenu::addModes(QMenu* menu) {
    QMenu* shuffleMenu = menu->addMenu(tr("Shuffle"));
    shuffleMenu->setObjectName(QStringLiteral("shuffleMenu"));
    auto* shuffleGroup = new QActionGroup(shuffleMenu);
    const std::pair<Core::ShuffleAlgorithm, QString> algorithms[] = {
        {Core::ShuffleAlgorithm::Random, tr("Random track")},
        {Core::ShuffleAlgorithm::WithoutRepeats, tr("Without repeats")}
    };
    for (const auto& [algorithm, name] : algorithms) {
        QAction* action = shuffleMenu->addAction(name, this, [this, algorithm] {
            parts.player->setShuffleAlgorithm(algorithm);
        });
        action->setCheckable(true);
        action->setChecked(parts.player->shuffleAlgorithm() == algorithm);
        shuffleGroup->addAction(action);
    }
    QAction* vibeAction = menu->addAction(tr("Continue with a vibe"), this, [this](bool on) {
        parts.player->setContinueWithVibe(on);  // CONT-02
    });
    vibeAction->setObjectName(QStringLiteral("continueWithVibe"));
    vibeAction->setCheckable(true);
    vibeAction->setChecked(parts.player->continuesWithVibe());
    vibeAction->setEnabled(parts.player->canSwitchVibe());

    QMenu* qualityMenu = menu->addMenu(tr("Quality"));
    qualityMenu->setObjectName(QStringLiteral("qualityMenu"));
    auto* qualityGroup = new QActionGroup(qualityMenu);
    const std::pair<Yandex::Quality, QString> qualities[] = {
        {Yandex::Quality::Standard, tr("Standard")}, {Yandex::Quality::Economy, tr("Economy: a third of the data")}
    };
    for (const auto& [quality, name] : qualities) {
        QAction* action = qualityMenu->addAction(name, this, [this, quality] {
            parts.yandex->setQuality(quality);
            parts.settings->setQuality(quality);
        });
        action->setCheckable(true);
        action->setChecked(parts.yandex->quality() == quality);
        qualityGroup->addAction(action);
    }
}

void MainMenu::addLooks(QMenu* menu) {
    QMenu* skinsMenu = menu->addMenu(tr("Skins"));
    const QStringList builtin =
        QDir(QStringLiteral(":/skins")).entryList({QStringLiteral("*.wsz")}, QDir::Files, QDir::Name);
    for (const QString& name : builtin) {
        const QString path = QStringLiteral(":/skins/") + name;
        skinsMenu->addAction(QString(name).chopped(4), this, [this, path] { Q_EMIT skinChosen(path); });
    }
    skinsMenu->addSeparator();
    QAction* loadSkin = skinsMenu->addAction(tr("Load a skin…"), this, [this] {
        QTimer::singleShot(0, this, &MainMenu::loadSkinFile);
    });
    loadSkin->setObjectName(QStringLiteral("loadSkinFile"));

    QMenu* sizeMenu = menu->addMenu(tr("Size"));
    auto* sizeGroup = new QActionGroup(sizeMenu);
    for (double scaleFactor : {1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0}) {
        QAction* action =
            sizeMenu->addAction(QStringLiteral("%1%").arg(qRound(scaleFactor * 100)), this, [this, scaleFactor] {
                parts.windows->setScale(scaleFactor);
            });
        action->setCheckable(true);
        action->setChecked(std::abs(parts.mainWindow->scale() - scaleFactor) < 1e-6);
        sizeGroup->addAction(action);
    }
    sizeMenu->addSeparator();
    QAction* doubleSizeAction = sizeMenu->addAction(tr("Double size"), this, [this] {
        parts.windows->setScale(DoubledOrBack(parts.mainWindow->scale()));
    });
    doubleSizeAction->setShortcut(QKeySequence(kDoubleSizeKey));

    // Named in each language, and with the English word, so that a wrong pick can be undone.
    QString languageTitle = tr("Language");
    if (languageTitle != QLatin1String("Language")) {
        languageTitle += QStringLiteral(" (Language)");
    }
    QMenu* languageMenu = menu->addMenu(languageTitle);
    auto* languageGroup = new QActionGroup(languageMenu);
    for (Language language : Languages()) {
        QAction* action = languageMenu->addAction(LanguageName(language), this, [this, language] {
            Q_EMIT languageChosen(language);
        });
        action->setCheckable(true);
        action->setChecked(language == parts.translations->language());
        languageGroup->addAction(action);
    }

    QAction* alwaysOnTopAction = menu->addAction(tr("Always on top"), this, [this](bool on) {
        parts.windows->setAlwaysOnTop(on);
    });
    alwaysOnTopAction->setCheckable(true);
    alwaysOnTopAction->setChecked(parts.mainWindow->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    if (parts.telemetry->available()) {
        QAction* telemetryAction = menu->addAction(tr("Send statistics"), this, [this](bool on) {
            parts.telemetry->setEnabled(on);
        });
        telemetryAction->setCheckable(true);
        telemetryAction->setChecked(parts.telemetry->enabled());
    }
}

}  // namespace App
