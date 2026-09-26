#include "ui/milkdrop_window.h"

#include "audio/audio_engine.h"
#include "skins/skin.h"
#include "vis/milkdrop_view.h"

#include <QActionGroup>
#include <QClipboard>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QMenu>
#include <QPainter>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QScreen>
#include <QUrl>

#include <algorithm>

namespace Ui {

namespace {
constexpr int kHistory = 100;
constexpr int kFpsPlaying = 60;
constexpr int kFpsIdle = 20;
}  // namespace

MilkdropWindow::MilkdropWindow(
    Audio::AudioEngine* engine,
    const QString& builtInDir,
    const QString& userDir,
    const Skins::Skin* skin,
    QWidget* parent
)
    : GenWindow(skin, QStringLiteral("MILKDROP"), parent)
    , audioEngine(engine)
    , builtInDirectory(builtInDir)
    , userDirectory(userDir) {
    setWindowTitle(QStringLiteral("QiYaa: Milkdrop"));
    presetList.load(builtInDirectory, userDirectory);
}

void MilkdropWindow::ensureView() {
    // Not in the constructor: probing OpenGL loads the GPU driver, which is
    // wasted on everyone who never opens Milkdrop.
    if (viewTried) {
        return;
    }
    viewTried = true;
    glProblem = Vis::MilkdropView::OpenGlProblem();
    if (!glProblem.isEmpty()) {
        qWarning("Milkdrop unavailable: %s", qPrintable(glProblem));
        update();
        return;
    }
    milkdropView = new Vis::MilkdropView(audioEngine);
    container = QWidget::createWindowContainer(milkdropView, this);
    container->setFocusPolicy(Qt::ClickFocus);
    wireView(milkdropView);
    placeView();
    container->show();
}

QString MilkdropWindow::failure() const {
    if (!glProblem.isEmpty()) {
        return glProblem;
    }
    return milkdropView ? milkdropView->failure() : QString();
}

MilkdropWindow::~MilkdropWindow() = default;

QString MilkdropWindow::currentPreset() const {
    return selectedIndex >= 0 && selectedIndex < presetList.size()
        ? presetList.at(selectedIndex).name
        : QString();
}

void MilkdropWindow::wireView(Vis::MilkdropView* view) {
    view->setTextureSearchPaths({userDirectory, userDirectory + QStringLiteral("/textures")});
    view->setPresetDuration(switchIntervalSeconds);
    view->setLocked(lockEnabled);
    connect(view, &Vis::MilkdropView::ready, this, [this, view] {
        if (selectedIndex < 0 && !presetList.isEmpty()) {
            selectedIndex = followingPreset();
        }
        if (selectedIndex >= 0) {
            view->loadPreset(presetList.data(selectedIndex), false);
        }
        if (selectedIndex >= 0) {
            Q_EMIT presetChanged(currentPreset(), false);
        }
    });
    connect(view, &Vis::MilkdropView::failed, this, [this, view](const QString& reason) {
        qWarning("Milkdrop unavailable: %s", qPrintable(reason));
        if (view == milkdropView) {
            container->hide();
            milkdropView->setRendering(false);
        } else {
            setFullScreenMode(false);
        }
        update();
    });
    connect(view, &Vis::MilkdropView::switchRequested, this, &MilkdropWindow::onSwitchRequested);
    connect(view, &Vis::MilkdropView::presetFailed, this, &MilkdropWindow::onPresetFailed);
    connect(view, &Vis::MilkdropView::staysBlack, this, &MilkdropWindow::onStaysBlack);
    connect(view, &Vis::MilkdropView::drawsPicture, this, [this] { blackInARow = 0; });
    view->setBlackWatch(musicPlaying && blackInARow <= 5);
    connect(view, &Vis::MilkdropView::doubleClicked, this, [this] {
        setFullScreenMode(!isFullScreenMode());
    });
    connect(view, &Vis::MilkdropView::contextMenuRequested, this, &MilkdropWindow::showMenu);
    connect(view, &Vis::MilkdropView::keyPressed, this, &MilkdropWindow::handleKey);
}

void MilkdropWindow::selectPreset(int index, bool smooth, bool byUser) {
    if (index < 0 || index >= presetList.size()) {
        return;
    }
    const QByteArray milk = presetList.data(index);
    if (milk.isEmpty()) {
        return;
    }
    if (selectedIndex >= 0 && selectedIndex != index) {
        history.append(selectedIndex);
        if (history.size() > kHistory) {
            history.removeFirst();
        }
    }
    selectedIndex = index;
    if (Vis::MilkdropView* view = fullView ? fullView.get() : milkdropView) {
        view->loadPreset(milk, smooth);
    }
    Q_EMIT presetChanged(currentPreset(), byUser);
    Q_EMIT settingsChanged();
}

void MilkdropWindow::selectPreset(const QString& name) {
    const int i = presetList.indexOf(name);
    if (i < 0) {
        return;
    }
    // Before the view is up this only picks what it starts with.
    if (!milkdropView || !milkdropView->isReady()) {
        selectedIndex = i;
        return;
    }
    selectPreset(i, false);
}

void MilkdropWindow::nextPreset() {
    failuresInARow = 0;
    selectPreset(followingPreset());
}

void MilkdropWindow::previousPreset() {
    failuresInARow = 0;
    int index = -1;
    if (shuffleEnabled && !history.isEmpty()) {
        index = history.takeLast();
        const int keep = selectedIndex;
        selectedIndex = -1;  // going back: don't record where we came from
        selectPreset(index);
        if (selectedIndex != index) {
            selectedIndex = keep;  // unreadable: stay
        }
        return;
    }
    int prev = presetList.previous(selectedIndex);
    for (int n = 0; n < presetList.size() && isBlack(prev); ++n) {
        prev = presetList.previous(prev);
    }
    selectPreset(prev);
}

void MilkdropWindow::reloadPresets() {
    const QString current = currentPreset();
    presetList.load(builtInDirectory, userDirectory);
    history.clear();
    selectedIndex = presetList.indexOf(current);
}

void MilkdropWindow::setShuffle(bool on) {
    if (on == shuffleEnabled) {
        return;
    }
    shuffleEnabled = on;
    Q_EMIT settingsChanged();
}

void MilkdropWindow::setLocked(bool on) {
    if (on == lockEnabled) {
        return;
    }
    lockEnabled = on;
    if (milkdropView) {
        milkdropView->setLocked(on);
    }
    if (fullView) {
        fullView->setLocked(on);
    }
    Q_EMIT settingsChanged();
}

void MilkdropWindow::setPresetSeconds(int seconds) {
    seconds = std::clamp(seconds, 5, 3600);
    if (seconds == switchIntervalSeconds) {
        return;
    }
    switchIntervalSeconds = seconds;
    if (milkdropView) {
        milkdropView->setPresetDuration(seconds);
    }
    if (fullView) {
        fullView->setPresetDuration(seconds);
    }
    Q_EMIT settingsChanged();
}

void MilkdropWindow::setPlaying(bool playing) {
    if (playing == musicPlaying) {
        return;
    }
    musicPlaying = playing;
    // Silence may legitimately fade a preset to black: judge only with music.
    if (milkdropView) {
        milkdropView->setBlackWatch(playing && blackInARow <= 5);
    }
    if (fullView) {
        fullView->setBlackWatch(playing && blackInARow <= 5);
    }
    updateRendering();
}

int MilkdropWindow::fps() const {
    return musicPlaying ? kFpsPlaying : kFpsIdle;
}

void MilkdropWindow::updateRendering() {
    if (!milkdropView) {
        return;
    }
    if (fullView) {
        milkdropView->setRendering(false);
        fullView->setRendering(true, fps());
    } else {
        milkdropView->setRendering(isVisible() && milkdropView->failure().isEmpty(), fps());
    }
}

void MilkdropWindow::setFullScreenMode(bool on) {
    if (on == isFullScreenMode()) {
        return;
    }
    if (on) {
        if (!milkdropView || !milkdropView->failure().isEmpty()) {
            return;
        }
        fullView = std::make_unique<Vis::MilkdropView>(audioEngine);
        wireView(fullView.get());
        fullView->setTitle(QStringLiteral("QiYaa: Milkdrop"));
        fullView->setCursor(Qt::BlankCursor);
        if (QScreen* targetScreen = screen()) {
            fullView->setScreen(targetScreen);
            fullView->setGeometry(targetScreen->geometry());
        }
        fullView->showFullScreen();
        fullView->requestActivate();
    } else {
        // Possibly called from one of its own event handlers: delete it later.
        Vis::MilkdropView* view = fullView.release();
        view->setRendering(false);
        view->hide();
        view->deleteLater();
        if (selectedIndex >= 0 && milkdropView) {
            milkdropView->loadPreset(presetList.data(selectedIndex), false);
        }
    }
    updateRendering();
}

int MilkdropWindow::followingPreset() const {
    if (presetList.isEmpty()) {
        return -1;
    }
    if (shuffleEnabled) {
        // Draw only from the presets that can be shown (not by retrying random
        // picks: those can all land on black ones).
        QList<int> candidates;
        for (int i = 0; i < presetList.size(); ++i) {
            if (i != selectedIndex && !isBlack(i)) {
                candidates.append(i);
            }
        }
        if (!candidates.isEmpty()) {
            return candidates.at(int(QRandomGenerator::global()->bounded(candidates.size())));
        }
    } else {
        int index = selectedIndex;  // -1 before the first one: then next() starts at 0
        const int others = selectedIndex >= 0 ? presetList.size() - 1 : presetList.size();
        for (int n = 0; n < others; ++n) {
            index = presetList.next(index);
            if (!isBlack(index)) {
                return index;
            }
        }
    }
    // Every other preset is black: stay on this one if it isn't, else don't get stuck.
    if (selectedIndex >= 0 && !isBlack(selectedIndex)) {
        return selectedIndex;
    }
    return shuffleEnabled ? presetList.random(selectedIndex) : presetList.next(selectedIndex);
}

bool MilkdropWindow::isBlack(int index) const {
    return index >= 0 && index < presetList.size()
        && blackPresetNames.contains(presetList.at(index).name);
}

QStringList MilkdropWindow::blackPresets() const {
    QStringList out(blackPresetNames.cbegin(), blackPresetNames.cend());
    out.sort();
    return out;
}

void MilkdropWindow::setBlackPresets(const QStringList& names) {
    blackPresetNames = QSet<QString>(names.cbegin(), names.cend());
    blackInARow = 0;
}

void MilkdropWindow::onStaysBlack() {
    const QString name = currentPreset();
    if (name.isEmpty()) {
        return;
    }
    const Vis::MilkdropView* view = fullView ? fullView.get() : milkdropView;
    qWarning(
        "Milkdrop: \"%s\" shows only black here (%s), skipping it", qPrintable(name),
        view ? qPrintable(view->glInfo()) : "?"
    );
    // Black one after another: something else is wrong (no sound reaching it,
    // a driver problem), so stop blaming presets.
    if (++blackInARow > 5) {
        qWarning("Milkdrop: many presets in a row stay black; not skipping any more");
        if (milkdropView) {
            milkdropView->setBlackWatch(false);
        }
        if (fullView) {
            fullView->setBlackWatch(false);
        }
        return;
    }
    blackPresetNames.insert(name);
    Q_EMIT settingsChanged();
    failuresInARow = 0;
    selectPreset(followingPreset(), false, false);
}

void MilkdropWindow::onSwitchRequested(bool hardCut) {
    if (lockEnabled || presetList.isEmpty()) {
        return;
    }
    failuresInARow = 0;
    blackInARow = 0;  // this one played its full time without going black
    selectPreset(followingPreset(), !hardCut, false);
}

void MilkdropWindow::onPresetFailed(const QString& message) {
    qWarning("Milkdrop preset \"%s\" failed: %s", qPrintable(currentPreset()), qPrintable(message));
    // projectM keeps showing the previous preset; move on to another one.
    if (++failuresInARow >= std::min(10, presetList.size())) {
        return;
    }
    selectPreset(followingPreset(), false, false);
}

void MilkdropWindow::handleKey(int key, Qt::KeyboardModifiers mods) {
    switch (key) {
        case Qt::Key_Space:
        case Qt::Key_N: nextPreset(); break;
        case Qt::Key_Backspace:
        case Qt::Key_P: previousPreset(); break;
        case Qt::Key_H:  // hard cut: no blending
            failuresInARow = 0;
            selectPreset(followingPreset(), false);
            break;
        case Qt::Key_R: setShuffle(!shuffleEnabled); break;
        case Qt::Key_L:
        case Qt::Key_ScrollLock: setLocked(!lockEnabled); break;
        case Qt::Key_F: setFullScreenMode(!isFullScreenMode()); break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (mods & Qt::AltModifier) {
                setFullScreenMode(!isFullScreenMode());
            }
            break;
        case Qt::Key_Escape: setFullScreenMode(false); break;
        case Qt::Key_K:  // Ctrl+Shift+K toggles the visualization, as in Winamp
            if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier)) {
                setFullScreenMode(false);
                Q_EMIT closeRequested();
            }
            break;
        case Qt::Key_Z:
        case Qt::Key_X:
        case Qt::Key_C:
        case Qt::Key_V:
        case Qt::Key_B:
        case Qt::Key_Left:
        case Qt::Key_Right: Q_EMIT transportKey(key); break;
        default: break;
    }
}

void MilkdropWindow::showMenu(const QPoint& globalPos) {
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction(QStringLiteral("Следующий пресет\tПробел"), this, &MilkdropWindow::nextPreset);
    menu->addAction(
        QStringLiteral("Предыдущий пресет\tBackspace"), this, &MilkdropWindow::previousPreset
    );
    if (!presetList.isEmpty()) {
        QMenu* list = menu->addMenu(QStringLiteral("Пресеты"));
        for (int i = 0; i < presetList.size(); ++i) {
            const QString label = isBlack(i)
                ? presetList.at(i).name + QStringLiteral("  (здесь чёрный)")
                : presetList.at(i).name;
            QAction* action = list->addAction(label, this, [this, i] {
                failuresInARow = 0;
                selectPreset(i);
            });
            action->setCheckable(true);
            action->setChecked(i == selectedIndex);
        }
    }
    menu->addSeparator();
    QAction* shuffle =
        menu->addAction(QStringLiteral("Случайный порядок\tR"), this, [this](bool on) {
            setShuffle(on);
        });
    shuffle->setCheckable(true);
    shuffle->setChecked(shuffleEnabled);
    QAction* lock = menu->addAction(QStringLiteral("Не переключать сам\tL"), this, [this](bool on) {
        setLocked(on);
    });
    lock->setCheckable(true);
    lock->setChecked(lockEnabled);
    QMenu* every = menu->addMenu(QStringLiteral("Менять пресет каждые"));
    auto* group = new QActionGroup(every);
    for (int s : {15, 30, 60, 120, 300}) {
        const QString label =
            s < 60 ? QStringLiteral("%1 с").arg(s) : QStringLiteral("%1 мин").arg(s / 60);
        QAction* action = every->addAction(label, this, [this, s] { setPresetSeconds(s); });
        action->setCheckable(true);
        action->setChecked(s == switchIntervalSeconds);
        group->addAction(action);
    }
    menu->addSeparator();
    QAction* full = menu->addAction(QStringLiteral("Во весь экран\tF"), this, [this](bool on) {
        setFullScreenMode(on);
    });
    full->setCheckable(true);
    full->setChecked(isFullScreenMode());
    full->setEnabled(failure().isEmpty());
    menu->addSeparator();
    menu->addAction(QStringLiteral("Открыть папку своих пресетов"), this, [this] {
        QDir().mkpath(userDirectory);
        QDesktopServices::openUrl(QUrl::fromLocalFile(userDirectory));
    });
    menu->addAction(QStringLiteral("Перечитать пресеты"), this, &MilkdropWindow::reloadPresets);
    menu->addAction(
            QStringLiteral("Скопировать название пресета"), this,
            [this] { QGuiApplication::clipboard()->setText(currentPreset()); }
    )->setEnabled(selectedIndex >= 0);
    if (!blackPresetNames.isEmpty()) {
        menu->addAction(
            QStringLiteral("Вернуть пропущенные чёрные пресеты (%1)").arg(blackPresetNames.size()),
            this,
            [this] {
                setBlackPresets({});
                if (milkdropView) {
                    milkdropView->setBlackWatch(musicPlaying);
                }
                if (fullView) {
                    fullView->setBlackWatch(musicPlaying);
                }
                Q_EMIT settingsChanged();
            }
        );
    }
    menu->popup(globalPos);
}

void MilkdropWindow::paintContent(QPainter& painter, const QRect& area) {
    painter.fillRect(area, Qt::black);
    const QString failure = this->failure();
    if (failure.isEmpty()) {
        return;
    }
    QFont f = painter.font();
    f.setPixelSize(9);
    painter.setFont(f);
    painter.setPen(QColor(0, 200, 0));
    painter.drawText(
        area.adjusted(4, 4, -4, -4), Qt::AlignCenter | Qt::TextWordWrap,
        QStringLiteral("Milkdrop недоступен: %1").arg(failure)
    );
}

bool MilkdropWindow::contentMousePress(QPoint, Qt::MouseButton button) {
    if (button == Qt::RightButton) {
        showMenu(QCursor::pos());
        return true;
    }
    return false;
}

void MilkdropWindow::placeView() {
    if (!container) {
        return;
    }
    const QRect rect = contentRect();
    const double s = scale();
    const int left = qRound(rect.left() * s), top = qRound(rect.top() * s);
    const int right = qRound((rect.right() + 1) * s), bottom = qRound((rect.bottom() + 1) * s);
    container->setGeometry(left, top, right - left, bottom - top);
}

void MilkdropWindow::resizeEvent(QResizeEvent* event) {
    GenWindow::resizeEvent(event);
    placeView();
}

void MilkdropWindow::showEvent(QShowEvent* event) {
    GenWindow::showEvent(event);
    ensureView();
    updateRendering();
}

void MilkdropWindow::hideEvent(QHideEvent* event) {
    GenWindow::hideEvent(event);
    setFullScreenMode(false);
    updateRendering();
}

}  // namespace Ui
