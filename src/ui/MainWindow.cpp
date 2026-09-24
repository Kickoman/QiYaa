#include "ui/MainWindow.h"

#include <QActionGroup>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileDialog>
#include <QMenu>
#include <QPainter>
#include <QWheelEvent>

#include "core/Player.h"
#include "skin/Skin.h"
#include "skin/SkinSprites.h"

namespace qiyaa {

using audio::AudioEngine;
using Sheet = Skin::Sheet;
namespace S = sprites;
namespace L = sprites::main;

namespace {

constexpr int kMarqueeStepMs = 220;
constexpr int kStatusShowMs = 3000;
const QString kMarqueeSeparator = QStringLiteral("  ***  ");

QString formatTime(double seconds) {
    const int s = std::max(0, int(seconds));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

bool contains(const QRect& r, QPoint p) {
    return p.x() >= r.x() && p.y() >= r.y() && p.x() < r.x() + r.width() && p.y() < r.y() + r.height();
}

}  // namespace

MainWindow::MainWindow(Player* player, const Skin* skin, QWidget* parent)
    : SkinnedWindow(skin, L::kSize, parent), m_player(player) {
    setWindowTitle(QStringLiteral("QiYaa"));
    setMouseTracking(false);
    m_blink.start();
    m_marqueeStep.start();

    connect(&m_timer, &QTimer::timeout, this, &MainWindow::tick);
    connect(m_player->engine(), &AudioEngine::stateChanged, this, [this] {
        refreshTimer();
        update();
    });
    connect(m_player, &Player::currentTrackChanged, this, [this] {
        m_marqueeOffset = 0;
        refreshTimer();
        update();
    });
    connect(m_player, &Player::statusMessage, this, &MainWindow::setStatusText);
    refreshTimer();
}

void MainWindow::setVolume(int v) {
    m_volume = std::clamp(v, 0, 100);
    m_player->engine()->setVolume(m_volume);
    update();
}

void MainWindow::setBalance(int b) {
    b = std::clamp(b, -100, 100);
    if (std::abs(b) < 8) b = 0;  // Winamp snaps to centre
    m_balance = b;
    m_player->engine()->setBalance(m_balance);
    update();
}

void MainWindow::setStatusText(const QString& text) {
    m_status = text;
    m_statusAge.start();
    refreshTimer();
    update();
}

// ------------------------------------------------------------------ timer

void MainWindow::refreshTimer() {
    // Only tick when something on screen actually changes: playback, a blinking
    // pause, a pending status text, or a marquee that has to scroll.
    const auto st = m_player->engine()->state();
    int interval = 0;
    if (st == AudioEngine::State::Playing || st == AudioEngine::State::Buffering) interval = 100;
    else if (st == AudioEngine::State::Paused) interval = 250;
    else if (!m_status.isEmpty() || Skin::textWidth(marqueeText()) > L::kMarquee.width()) interval = kMarqueeStepMs;

    if (interval == 0 || isMinimized()) m_timer.stop();
    else if (!m_timer.isActive() || m_timer.interval() != interval) m_timer.start(interval);
}

void MainWindow::tick() {
    m_player->engine()->poll();
    if (!m_status.isEmpty() && m_statusAge.elapsed() > kStatusShowMs) m_status.clear();
    if (m_status.isEmpty() && m_marqueeStep.elapsed() >= kMarqueeStepMs && m_pressed != Element::Marquee) {
        m_marqueeStep.restart();
        ++m_marqueeOffset;
    }
    refreshTimer();
    update();
}

// ------------------------------------------------------------------ painting

QString MainWindow::marqueeText() const {
    if (!m_status.isEmpty()) return m_status;
    if (m_pressed == Element::Volume) return QStringLiteral("VOLUME: %1%").arg(m_volume);
    if (m_pressed == Element::Balance) {
        if (m_balance == 0) return QStringLiteral("BALANCE: CENTER");
        return QStringLiteral("BALANCE: %1% %2").arg(std::abs(m_balance)).arg(m_balance < 0 ? "LEFT" : "RIGHT");
    }
    if (m_pressed == Element::Position && m_seekPreview >= 0) {
        const double dur = m_player->durationSeconds();
        return QStringLiteral("SEEK TO: %1/%2 (%3%)")
            .arg(formatTime(m_seekPreview * dur), formatTime(dur))
            .arg(int(m_seekPreview * 100));
    }
    const auto* t = m_player->currentTrack();
    if (!t) return QStringLiteral("QiYaa %1").arg(QApplication::applicationVersion());
    return QStringLiteral("%1. %2 (%3)").arg(m_player->currentIndex() + 1).arg(t->displayTitle(), formatTime(t->durationMs / 1000.0));
}

void MainWindow::drawButton(QPainter& p, Element e, const QPoint& at, const QRect& normal, const QRect& pressed) const {
    const bool down = m_pressed == e && m_pressedInside;
    const Sheet sheet = (e == Element::Options || e == Element::Minimize || e == Element::Shade || e == Element::Close)
                            ? Sheet::TitleBar
                            : Sheet::CButtons;
    skin().draw(p, sheet, down ? pressed : normal, at);
}

void MainWindow::drawTime(QPainter& p) const {
    const auto st = m_player->engine()->state();
    if (st == AudioEngine::State::Stopped) return;
    if (st == AudioEngine::State::Paused && (m_blink.elapsed() / 1000) % 2 == 1) return;

    const int secs = int(m_player->engine()->positionSeconds());
    const int mm = std::min(secs / 60, 99);
    const int ss = secs % 60;
    const int digits[4] = {mm / 10, mm % 10, ss / 10, ss % 10};
    const int offsets[4] = {9, 21, 39, 51};
    for (int i = 0; i < 4; ++i)
        skin().draw(p, Sheet::Numbers, S::digit(digits[i]), L::kTime + QPoint(offsets[i], 0));
}

void MainWindow::paintSkin(QPainter& p) {
    const Skin& sk = skin();
    const auto st = m_player->engine()->state();
    const bool stopped = st == AudioEngine::State::Stopped;

    sk.draw(p, Sheet::Main, S::kMainBackground, {0, 0});
    sk.draw(p, Sheet::TitleBar, isActiveWindow() ? S::kTitleBarSelected : S::kTitleBar, {0, 0});
    drawButton(p, Element::Options, L::kOptions, S::kOptionsButton, S::kOptionsButtonDown);
    drawButton(p, Element::Minimize, L::kMinimize, S::kMinimizeButton, S::kMinimizeButtonDown);
    drawButton(p, Element::Shade, L::kShade, S::kShadeButton, S::kShadeButtonDown);
    drawButton(p, Element::Close, L::kClose, S::kCloseButton, S::kCloseButtonDown);
    sk.draw(p, Sheet::TitleBar, S::kClutterBar, L::kClutter);

    // Status: play/pause/stop indicator + time.
    const QRect indicator = st == AudioEngine::State::Paused ? S::kPausedIndicator
                            : stopped                        ? S::kStoppedIndicator
                                                             : S::kPlayingIndicator;
    sk.draw(p, Sheet::PlayPaus, indicator, L::kPlayPause);
    if (!stopped && st != AudioEngine::State::Paused) {
        const bool working = st == AudioEngine::State::Buffering;
        sk.draw(p, Sheet::PlayPaus, QRect(working ? 39 : 36, 0, 3, 9), L::kPlayPause - QPoint(2, 0));
    }
    drawTime(p);

    // Marquee.
    {
        p.save();
        p.setClipRect(L::kMarquee);
        QString text = marqueeText();
        const bool scroll = m_status.isEmpty() && m_pressed == Element::None && Skin::textWidth(text) > L::kMarquee.width();
        if (scroll) {
            const QString loop = text + kMarqueeSeparator;
            const int n = int(loop.size());
            const int off = n ? m_marqueeOffset % n : 0;
            text = loop.mid(off) + loop.left(off) + loop;
        }
        sk.drawText(p, L::kMarquee.topLeft(), text, L::kMarquee.width() + S::kCharW);
        p.restore();
    }

    // kbps / kHz / mono-stereo.
    if (!stopped) {
        const int kbps = m_player->currentBitrate();
        const int khz = (m_player->engine()->sourceSampleRate() + 500) / 1000;
        if (kbps > 0) {
            const QString t = QString::number(kbps).rightJustified(3, u' ').right(3);
            sk.drawText(p, L::kKbps, t);
        }
        if (khz > 0) sk.drawText(p, L::kKhz, QString::number(khz).rightJustified(2, u' ').right(2));
    }
    const int ch = stopped ? 0 : m_player->engine()->sourceChannels();
    sk.draw(p, Sheet::MonoSter, ch == 1 ? S::kMonoSelected : S::kMono, L::kMono);
    sk.draw(p, Sheet::MonoSter, ch >= 2 ? S::kStereoSelected : S::kStereo, L::kStereo);

    // Volume.
    {
        const int frame = int(std::lround(m_volume / 100.0 * 28));
        const int offset = std::max(0, (frame - 1) * S::kSliderFrameStep);
        sk.draw(p, Sheet::Volume, QRect(0, offset, L::kVolume.width(), S::kSliderFrameH), L::kVolume.topLeft());
        const int x = int(std::lround(m_volume / 100.0 * (L::kVolume.width() - S::kVolumeThumb.width())));
        sk.draw(p, Sheet::Volume, m_pressed == Element::Volume ? S::kVolumeThumbSelected : S::kVolumeThumb,
                L::kVolume.topLeft() + QPoint(x, 1));
    }
    // Balance.
    {
        const int offset = int(std::abs(m_balance) / 100.0 * 27) * S::kSliderFrameStep;
        sk.draw(p, Sheet::Balance, QRect(9, offset, L::kBalance.width(), S::kSliderFrameH), L::kBalance.topLeft());
        const int x = int(std::lround((m_balance + 100) / 200.0 * (L::kBalance.width() - S::kBalanceThumb.width())));
        sk.draw(p, Sheet::Balance, m_pressed == Element::Balance ? S::kBalanceThumbSelected : S::kBalanceThumb,
                L::kBalance.topLeft() + QPoint(x, 1));
    }

    // EQ / PL toggles.
    auto toggle = [&](Element e, const S::ToggleSprite& spr, bool on, QPoint at) {
        const bool down = m_pressed == e && m_pressedInside;
        sk.draw(p, Sheet::ShufRep, on ? (down ? spr.onPressed : spr.on) : (down ? spr.offPressed : spr.off), at);
    };
    toggle(Element::EqToggle, S::kEqButton, m_eqOn, L::kEqButton);
    toggle(Element::PlToggle, S::kPlButton, m_plOn, L::kPlButton);

    // Position bar.
    sk.draw(p, Sheet::PosBar, S::kPositionBackground, L::kPosition.topLeft());
    const double dur = m_player->durationSeconds();
    if (!stopped && dur > 0) {
        const double frac = m_seekPreview >= 0 ? m_seekPreview
                                               : std::clamp(m_player->engine()->positionSeconds() / dur, 0.0, 1.0);
        const int x = int(frac * (L::kPosition.width() - S::kPositionThumb.width()));
        sk.draw(p, Sheet::PosBar, m_pressed == Element::Position ? S::kPositionThumbSelected : S::kPositionThumb,
                L::kPosition.topLeft() + QPoint(x, 0));
    }

    // Transport.
    drawButton(p, Element::Previous, L::kPrevious, S::kPrevious.normal, S::kPrevious.pressed);
    drawButton(p, Element::Play, L::kPlay, S::kPlay.normal, S::kPlay.pressed);
    drawButton(p, Element::Pause, L::kPause, S::kPause.normal, S::kPause.pressed);
    drawButton(p, Element::Stop, L::kStop, S::kStop.normal, S::kStop.pressed);
    drawButton(p, Element::Next, L::kNext, S::kNext.normal, S::kNext.pressed);
    drawButton(p, Element::Eject, L::kEject, S::kEject.normal, S::kEject.pressed);
    toggle(Element::Shuffle, S::kShuffle, m_player->shuffle(), L::kShuffle);
    toggle(Element::Repeat, S::kRepeat, m_player->repeat(), L::kRepeat);
}

// ------------------------------------------------------------------ input

MainWindow::Element MainWindow::hitTest(QPoint p) const {
    struct Area {
        QRect rect;
        Element e;
    };
    static const Area areas[] = {
        {{L::kOptions, QSize(9, 9)}, Element::Options},
        {{L::kMinimize, QSize(9, 9)}, Element::Minimize},
        {{L::kShade, QSize(9, 9)}, Element::Shade},
        {{L::kClose, QSize(9, 9)}, Element::Close},
        {{L::kPrevious, QSize(23, 18)}, Element::Previous},
        {{L::kPlay, QSize(23, 18)}, Element::Play},
        {{L::kPause, QSize(23, 18)}, Element::Pause},
        {{L::kStop, QSize(23, 18)}, Element::Stop},
        {{L::kNext, QSize(22, 18)}, Element::Next},
        {{L::kEject, QSize(22, 16)}, Element::Eject},
        {{L::kShuffle, QSize(47, 15)}, Element::Shuffle},
        {{L::kRepeat, QSize(28, 15)}, Element::Repeat},
        {{L::kEqButton, QSize(23, 12)}, Element::EqToggle},
        {{L::kPlButton, QSize(23, 12)}, Element::PlToggle},
        {L::kVolume, Element::Volume},
        {L::kBalance, Element::Balance},
        {L::kPosition, Element::Position},
        {L::kMarquee.adjusted(0, -3, 0, 3), Element::Marquee},
    };
    for (const Area& a : areas)
        if (contains(a.rect, p)) return a.e;
    return Element::None;
}

bool MainWindow::isDragArea(QPoint p) const {
    // Winamp lets you drag the main window by any spot that isn't a control.
    const Element e = hitTest(p);
    return e == Element::None || e == Element::Marquee;
}

bool MainWindow::skinMousePress(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton) return false;
    const Element e = hitTest(pos);
    if (e == Element::None || e == Element::Marquee) return false;  // marquee drags the window
    if (e == Element::Position && m_player->engine()->state() == AudioEngine::State::Stopped) return true;
    m_pressed = e;
    m_pressedInside = true;
    updateSliderFromMouse(e, pos);
    update();
    return true;
}

void MainWindow::skinMouseMove(QPoint pos) {
    if (m_pressed == Element::None) return;
    if (m_pressed == Element::Volume || m_pressed == Element::Balance || m_pressed == Element::Position) {
        updateSliderFromMouse(m_pressed, pos);
    } else {
        m_pressedInside = hitTest(pos) == m_pressed;
    }
    update();
}

void MainWindow::skinMouseRelease(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton || m_pressed == Element::None) return;
    const Element e = m_pressed;
    const bool inside = hitTest(pos) == e;
    if (e == Element::Position && m_seekPreview >= 0) m_player->seekFraction(m_seekPreview);
    m_pressed = Element::None;
    m_seekPreview = -1;
    if (inside && e != Element::Volume && e != Element::Balance && e != Element::Position) activate(e);
    update();
}

void MainWindow::updateSliderFromMouse(Element e, QPoint p) {
    auto fraction = [&](const QRect& r, int thumbW) {
        const double x = p.x() - r.x() - thumbW / 2.0;
        return std::clamp(x / double(r.width() - thumbW), 0.0, 1.0);
    };
    switch (e) {
    case Element::Volume: setVolume(int(std::lround(fraction(L::kVolume, S::kVolumeThumb.width()) * 100))); break;
    case Element::Balance: setBalance(int(std::lround(fraction(L::kBalance, S::kBalanceThumb.width()) * 200 - 100))); break;
    case Element::Position: m_seekPreview = fraction(L::kPosition, S::kPositionThumb.width()); break;
    default: break;
    }
}

void MainWindow::activate(Element e) {
    switch (e) {
    case Element::Options: buildMenu()->popup(mapToGlobal(QPoint(L::kOptions.x(), L::kOptions.y() + 9) * scale())); break;
    case Element::Minimize: showMinimized(); break;
    case Element::Shade: setStatusText(QStringLiteral("Shade mode: coming in stage 2")); break;
    case Element::Close: close(); break;
    case Element::Previous: m_player->previous(); break;
    case Element::Play: m_player->play(); break;
    case Element::Pause: m_player->pause(); break;
    case Element::Stop: m_player->stop(); break;
    case Element::Next: m_player->next(); break;
    case Element::Eject: {
        const QString f = QFileDialog::getOpenFileName(this, tr("Load skin"), QDir::homePath(),
                                                       tr("Winamp skins (*.wsz *.zip)"));
        if (!f.isEmpty()) Q_EMIT skinRequested(f);
        break;
    }
    case Element::Shuffle: m_player->setShuffle(!m_player->shuffle()); break;
    case Element::Repeat: m_player->setRepeat(!m_player->repeat()); break;
    case Element::EqToggle: m_eqOn = !m_eqOn; setStatusText(QStringLiteral("Equalizer: stage 1")); break;
    case Element::PlToggle: m_plOn = !m_plOn; setStatusText(QStringLiteral("Playlist: stage 1")); break;
    default: break;
    }
}

void MainWindow::wheelEvent(QWheelEvent* e) {
    const int steps = e->angleDelta().y() / 120;
    if (steps != 0) {
        setVolume(m_volume + steps * 4);
        setStatusText(QStringLiteral("VOLUME: %1%").arg(m_volume));
    }
}

void MainWindow::contextMenuEvent(QContextMenuEvent* e) {
    buildMenu()->popup(e->globalPos());
}

QMenu* MainWindow::buildMenu() {
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    QMenu* skins = menu->addMenu(tr("Skins"));
    const QStringList builtin = QDir(QStringLiteral(":/skins")).entryList({QStringLiteral("*.wsz")}, QDir::Files, QDir::Name);
    for (const QString& name : builtin) {
        const QString path = QStringLiteral(":/skins/") + name;
        skins->addAction(QString(name).chopped(4), this, [this, path] { Q_EMIT skinRequested(path); });
    }
    skins->addSeparator();
    skins->addAction(tr("Load skin..."), this, [this] { activate(Element::Eject); });

    QAction* dbl = menu->addAction(tr("Double size"));
    dbl->setCheckable(true);
    dbl->setChecked(scale() == 2);
    connect(dbl, &QAction::toggled, this, [this](bool on) { Q_EMIT scaleRequested(on ? 2 : 1); });

    QAction* top = menu->addAction(tr("Always on top"));
    top->setCheckable(true);
    top->setChecked(windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    connect(top, &QAction::toggled, this, [this](bool on) { Q_EMIT alwaysOnTopRequested(on); });

    menu->addSeparator();
    menu->addAction(tr("Exit"), qApp, &QApplication::quit);
    return menu;
}

}  // namespace qiyaa
