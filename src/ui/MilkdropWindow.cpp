#include "ui/MilkdropWindow.h"

#include <algorithm>

#include <QActionGroup>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QMenu>
#include <QPainter>
#include <QResizeEvent>
#include <QScreen>
#include <QUrl>

#include "audio/AudioEngine.h"
#include "skin/Skin.h"
#include "vis/MilkdropView.h"

namespace qiyaa {

namespace {
constexpr int kHistory = 100;
constexpr int kFpsPlaying = 60;
constexpr int kFpsIdle = 20;
}  // namespace

MilkdropWindow::MilkdropWindow(audio::AudioEngine* engine, const QString& builtInDir, const QString& userDir,
                               const Skin* skin, QWidget* parent)
    : GenWindow(skin, QStringLiteral("MILKDROP"), parent), m_engine(engine), m_builtInDir(builtInDir), m_userDir(userDir) {
    setWindowTitle(QStringLiteral("QiYaa: Milkdrop"));
    m_presets.load(m_builtInDir, m_userDir);
}

void MilkdropWindow::ensureView() {
    // Not in the constructor: probing OpenGL loads the GPU driver, which is
    // wasted on everyone who never opens Milkdrop.
    if (m_viewTried) return;
    m_viewTried = true;
    m_glProblem = MilkdropView::openGLProblem();
    if (!m_glProblem.isEmpty()) {
        qWarning("Milkdrop unavailable: %s", qPrintable(m_glProblem));
        update();
        return;
    }
    m_view = new MilkdropView(m_engine);
    m_container = QWidget::createWindowContainer(m_view, this);
    m_container->setFocusPolicy(Qt::ClickFocus);
    wireView(m_view);
    placeView();
    m_container->show();
}

QString MilkdropWindow::failure() const {
    if (!m_glProblem.isEmpty()) return m_glProblem;
    return m_view ? m_view->failure() : QString();
}

MilkdropWindow::~MilkdropWindow() = default;

QString MilkdropWindow::currentPreset() const {
    return m_current >= 0 && m_current < m_presets.size() ? m_presets.at(m_current).name : QString();
}

void MilkdropWindow::wireView(MilkdropView* view) {
    view->setTextureSearchPaths({m_userDir, m_userDir + QStringLiteral("/textures")});
    view->setPresetDuration(m_seconds);
    view->setLocked(m_locked);
    connect(view, &MilkdropView::ready, this, [this, view] {
        if (m_current < 0 && !m_presets.isEmpty()) m_current = m_shuffle ? m_presets.random(-1) : 0;
        if (m_current >= 0) view->loadPreset(m_presets.data(m_current), false);
        if (m_current >= 0) Q_EMIT presetChanged(currentPreset());
    });
    connect(view, &MilkdropView::failed, this, [this, view](const QString& reason) {
        qWarning("Milkdrop unavailable: %s", qPrintable(reason));
        if (view == m_view) {
            m_container->hide();
            m_view->setRendering(false);
        } else {
            setFullScreenMode(false);
        }
        update();
    });
    connect(view, &MilkdropView::switchRequested, this, &MilkdropWindow::onSwitchRequested);
    connect(view, &MilkdropView::presetFailed, this, &MilkdropWindow::onPresetFailed);
    connect(view, &MilkdropView::doubleClicked, this, [this] { setFullScreenMode(!isFullScreenMode()); });
    connect(view, &MilkdropView::contextMenuRequested, this, &MilkdropWindow::showMenu);
    connect(view, &MilkdropView::keyPressed, this, &MilkdropWindow::handleKey);
}

void MilkdropWindow::selectPreset(int index, bool smooth) {
    if (index < 0 || index >= m_presets.size()) return;
    const QByteArray milk = m_presets.data(index);
    if (milk.isEmpty()) return;
    if (m_current >= 0 && m_current != index) {
        m_history.append(m_current);
        if (m_history.size() > kHistory) m_history.removeFirst();
    }
    m_current = index;
    if (MilkdropView* v = m_fullView ? m_fullView.get() : m_view) v->loadPreset(milk, smooth);
    Q_EMIT presetChanged(currentPreset());
    Q_EMIT settingsChanged();
}

void MilkdropWindow::selectPreset(const QString& name) {
    const int i = m_presets.indexOf(name);
    if (i < 0) return;
    // Before the view is up this only picks what it starts with.
    if (!m_view || !m_view->isReady()) {
        m_current = i;
        return;
    }
    selectPreset(i, false);
}

void MilkdropWindow::nextPreset() {
    m_failuresInARow = 0;
    selectPreset(m_shuffle ? m_presets.random(m_current) : m_presets.next(m_current));
}

void MilkdropWindow::previousPreset() {
    m_failuresInARow = 0;
    int index = -1;
    if (m_shuffle && !m_history.isEmpty()) {
        index = m_history.takeLast();
        const int keep = m_current;
        m_current = -1;  // going back: don't record where we came from
        selectPreset(index);
        if (m_current != index) m_current = keep;  // unreadable: stay
        return;
    }
    selectPreset(m_presets.previous(m_current));
}

void MilkdropWindow::reloadPresets() {
    const QString current = currentPreset();
    m_presets.load(m_builtInDir, m_userDir);
    m_history.clear();
    m_current = m_presets.indexOf(current);
}

void MilkdropWindow::setShuffle(bool on) {
    if (on == m_shuffle) return;
    m_shuffle = on;
    Q_EMIT settingsChanged();
}

void MilkdropWindow::setLocked(bool on) {
    if (on == m_locked) return;
    m_locked = on;
    if (m_view) m_view->setLocked(on);
    if (m_fullView) m_fullView->setLocked(on);
    Q_EMIT settingsChanged();
}

void MilkdropWindow::setPresetSeconds(int seconds) {
    seconds = std::clamp(seconds, 5, 3600);
    if (seconds == m_seconds) return;
    m_seconds = seconds;
    if (m_view) m_view->setPresetDuration(seconds);
    if (m_fullView) m_fullView->setPresetDuration(seconds);
    Q_EMIT settingsChanged();
}

void MilkdropWindow::setPlaying(bool playing) {
    if (playing == m_playing) return;
    m_playing = playing;
    updateRendering();
}

int MilkdropWindow::fps() const {
    return m_playing ? kFpsPlaying : kFpsIdle;
}

void MilkdropWindow::updateRendering() {
    if (!m_view) return;
    if (m_fullView) {
        m_view->setRendering(false);
        m_fullView->setRendering(true, fps());
    } else {
        m_view->setRendering(isVisible() && m_view->failure().isEmpty(), fps());
    }
}

void MilkdropWindow::setFullScreenMode(bool on) {
    if (on == isFullScreenMode()) return;
    if (on) {
        if (!m_view || !m_view->failure().isEmpty()) return;
        m_fullView = std::make_unique<MilkdropView>(m_engine);
        wireView(m_fullView.get());
        m_fullView->setTitle(QStringLiteral("QiYaa: Milkdrop"));
        m_fullView->setCursor(Qt::BlankCursor);
        if (QScreen* s = screen()) {
            m_fullView->setScreen(s);
            m_fullView->setGeometry(s->geometry());
        }
        m_fullView->showFullScreen();
        m_fullView->requestActivate();
    } else {
        // Possibly called from one of its own event handlers: delete it later.
        MilkdropView* v = m_fullView.release();
        v->setRendering(false);
        v->hide();
        v->deleteLater();
        if (m_current >= 0 && m_view) m_view->loadPreset(m_presets.data(m_current), false);
    }
    updateRendering();
}

void MilkdropWindow::onSwitchRequested(bool hardCut) {
    if (m_locked || m_presets.isEmpty()) return;
    m_failuresInARow = 0;
    selectPreset(m_shuffle ? m_presets.random(m_current) : m_presets.next(m_current), !hardCut);
}

void MilkdropWindow::onPresetFailed(const QString& message) {
    qWarning("Milkdrop preset \"%s\" failed: %s", qPrintable(currentPreset()), qPrintable(message));
    // projectM keeps showing the previous preset; move on to another one.
    if (++m_failuresInARow >= std::min(10, m_presets.size())) return;
    selectPreset(m_shuffle ? m_presets.random(m_current) : m_presets.next(m_current), false);
}

void MilkdropWindow::handleKey(int key, Qt::KeyboardModifiers mods) {
    switch (key) {
    case Qt::Key_Space:
    case Qt::Key_N: nextPreset(); break;
    case Qt::Key_Backspace:
    case Qt::Key_P: previousPreset(); break;
    case Qt::Key_H:  // hard cut: no blending
        m_failuresInARow = 0;
        selectPreset(m_shuffle ? m_presets.random(m_current) : m_presets.next(m_current), false);
        break;
    case Qt::Key_R: setShuffle(!m_shuffle); break;
    case Qt::Key_L:
    case Qt::Key_ScrollLock: setLocked(!m_locked); break;
    case Qt::Key_F: setFullScreenMode(!isFullScreenMode()); break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (mods & Qt::AltModifier) setFullScreenMode(!isFullScreenMode());
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
    menu->addAction(QStringLiteral("Предыдущий пресет\tBackspace"), this, &MilkdropWindow::previousPreset);
    if (!m_presets.isEmpty()) {
        QMenu* list = menu->addMenu(QStringLiteral("Пресеты"));
        for (int i = 0; i < m_presets.size(); ++i) {
            QAction* a = list->addAction(m_presets.at(i).name, this, [this, i] {
                m_failuresInARow = 0;
                selectPreset(i);
            });
            a->setCheckable(true);
            a->setChecked(i == m_current);
        }
    }
    menu->addSeparator();
    QAction* shuffle = menu->addAction(QStringLiteral("Случайный порядок\tR"), this, [this](bool on) { setShuffle(on); });
    shuffle->setCheckable(true);
    shuffle->setChecked(m_shuffle);
    QAction* lock = menu->addAction(QStringLiteral("Не переключать сам\tL"), this, [this](bool on) { setLocked(on); });
    lock->setCheckable(true);
    lock->setChecked(m_locked);
    QMenu* every = menu->addMenu(QStringLiteral("Менять пресет каждые"));
    auto* group = new QActionGroup(every);
    for (int s : {15, 30, 60, 120, 300}) {
        const QString label = s < 60 ? QStringLiteral("%1 с").arg(s) : QStringLiteral("%1 мин").arg(s / 60);
        QAction* a = every->addAction(label, this, [this, s] { setPresetSeconds(s); });
        a->setCheckable(true);
        a->setChecked(s == m_seconds);
        group->addAction(a);
    }
    menu->addSeparator();
    QAction* full = menu->addAction(QStringLiteral("Во весь экран\tF"), this, [this](bool on) { setFullScreenMode(on); });
    full->setCheckable(true);
    full->setChecked(isFullScreenMode());
    full->setEnabled(failure().isEmpty());
    menu->addSeparator();
    menu->addAction(QStringLiteral("Открыть папку своих пресетов"), this, [this] {
        QDir().mkpath(m_userDir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_userDir));
    });
    menu->addAction(QStringLiteral("Перечитать пресеты"), this, &MilkdropWindow::reloadPresets);
    menu->popup(globalPos);
}

void MilkdropWindow::paintContent(QPainter& p, const QRect& area) {
    p.fillRect(area, Qt::black);
    const QString failure = this->failure();
    if (failure.isEmpty()) return;
    QFont f = p.font();
    f.setPixelSize(9);
    p.setFont(f);
    p.setPen(QColor(0, 200, 0));
    p.drawText(area.adjusted(4, 4, -4, -4), Qt::AlignCenter | Qt::TextWordWrap,
               QStringLiteral("Milkdrop недоступен: %1").arg(failure));
}

bool MilkdropWindow::contentMousePress(QPoint, Qt::MouseButton button) {
    if (button == Qt::RightButton) {
        showMenu(QCursor::pos());
        return true;
    }
    return false;
}

void MilkdropWindow::placeView() {
    if (!m_container) return;
    const QRect r = contentRect();
    const double s = scale();
    const int left = qRound(r.left() * s), top = qRound(r.top() * s);
    const int right = qRound((r.right() + 1) * s), bottom = qRound((r.bottom() + 1) * s);
    m_container->setGeometry(left, top, right - left, bottom - top);
}

void MilkdropWindow::resizeEvent(QResizeEvent* e) {
    GenWindow::resizeEvent(e);
    placeView();
}

void MilkdropWindow::showEvent(QShowEvent* e) {
    GenWindow::showEvent(e);
    ensureView();
    updateRendering();
}

void MilkdropWindow::hideEvent(QHideEvent* e) {
    GenWindow::hideEvent(e);
    setFullScreenMode(false);
    updateRendering();
}

}  // namespace qiyaa
