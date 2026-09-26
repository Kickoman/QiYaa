#include "ui/main_window.h"

#include "core/player.h"
#include "skins/skin.h"
#include "skins/sprites.h"

#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace Ui {

using Audio::AudioEngine;
using TSheet = Skins::Skin::Sheet;
using Skins::MainWindowSprites;

namespace {

constexpr int kMarqueeStepMs = 220;
constexpr int kVisFrameMs = 33;  // ~30 fps while the visualization is animating
constexpr int kFullRepaintMs = 100;  // time display, position bar
constexpr int kVisSamples = 1024;
constexpr int kStatusShowMs = 3000;
const QString kMarqueeSeparator = QStringLiteral("  ***  ");

QString FormatTime(double seconds) {
    const int s = std::max(0, int(seconds));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

bool Contains(const QRect& r, QPoint p) {
    return p.x() >= r.x() && p.y() >= r.y() && p.x() < r.x() + r.width()
        && p.y() < r.y() + r.height();
}

}  // namespace

MainWindow::MainWindow(Core::Player* player, const Skins::Skin* skin, QWidget* parent)
    : SkinnedWindow(skin, Skins::MainWindowSprites::kSize, parent)
    , corePlayer(player)
    , analyzer(kVisSamples) {
    setWindowTitle(QStringLiteral("QiYaa"));
    setMouseTracking(false);
    setDragsDockedWindows(true);
    blink.start();
    marqueeStep.start();
    lastFullRepaint.start();
    visL.resize(kVisSamples);
    visR.resize(kVisSamples);
    visMono.resize(kVisSamples);
    setVisMode(VisMode::Spectrum);

    connect(&timer, &QTimer::timeout, this, &MainWindow::tick);
    connect(corePlayer->engine(), &Audio::AudioEngine::stateChanged, this, [this] {
        refreshTimer();
        update();
    });
    connect(corePlayer, &Core::Player::currentTrackChanged, this, [this] {
        marqueeOffset = 0;
        refreshTimer();
        update();
    });
    connect(corePlayer, &Core::Player::statusMessage, this, &MainWindow::setStatusText);
    refreshTimer();
}

MainWindow::~MainWindow() = default;

void MainWindow::setEqButton(bool on) {
    eqOn = on;
    update();
}

void MainWindow::setPlButton(bool on) {
    plOn = on;
    update();
}

void MainWindow::setVisMode(VisMode mode) {
    visualizationMode = mode;
    switch (mode) {
        case VisMode::Spectrum: visualizer = Vis::MakeSpectrum(); break;
        case VisMode::Oscilloscope: visualizer = Vis::MakeOscilloscope(); break;
        case VisMode::Off: visualizer.reset(); break;
    }
    refreshTimer();
    update();
}

void MainWindow::setShowsRemainingTime(bool on) {
    remainingTimeShown = on;
    update();
}

QPoint MainWindow::globalAt(QPoint skinPos) const {
    return mapToGlobal(QPoint(qRound(skinPos.x() * scale()), qRound(skinPos.y() * scale())));
}

void MainWindow::setVolume(int v) {
    v = std::clamp(v, 0, 100);
    const bool changed = v != volumePercent;
    volumePercent = v;
    corePlayer->engine()->setVolume(volumePercent);
    update();
    if (changed) {
        Q_EMIT volumeChanged(volumePercent);
    }
}

void MainWindow::setShaded(bool shaded) {
    if (shaded == isShaded()) {
        return;
    }
    applyShade(shaded, shaded ? QSize(275, 14) : Skins::MainWindowSprites::kSize);
    refreshTimer();
    update();
}

void MainWindow::setBalance(int b) {
    b = std::clamp(b, -100, 100);
    if (std::abs(b) < 8) {
        b = 0;  // Winamp snaps to centre
    }
    const bool changed = b != balancePercent;
    balancePercent = b;
    corePlayer->engine()->setBalance(balancePercent);
    update();
    if (changed) {
        Q_EMIT balanceChanged(balancePercent);
    }
}

void MainWindow::setStatusText(const QString& text) {
    statusText = text;
    statusAge.start();
    refreshTimer();
    update();
}

// ------------------------------------------------------------------ timer

void MainWindow::refreshTimer() {
    // Only tick when something on screen actually changes: playback, a blinking
    // pause, a pending status text, or a marquee that has to scroll.
    const auto st = corePlayer->engine()->state();
    const bool playing =
        st == Audio::AudioEngine::State::Playing || st == Audio::AudioEngine::State::Buffering;
    visActive = playing && visualizer && isVisible() && !isMinimized() && !isShaded();
    int interval = 0;
    if (visActive) {
        interval = kVisFrameMs;
    } else if (playing) {
        interval = kFullRepaintMs;
    } else if (st == Audio::AudioEngine::State::Paused) {
        interval = 250;
    } else if (!statusText.isEmpty()
               || Skins::Skin::TextWidth(marqueeText())
                   > Skins::MainWindowSprites::kMarquee.width()) {
        interval = kMarqueeStepMs;
    }

    if (!visActive && visualizer) {
        visualizer->reset();
    }
    if (interval == 0 || isMinimized()) {
        timer.stop();
    } else if (!timer.isActive() || timer.interval() != interval) {
        timer.start(interval);
    }
}

void MainWindow::updateVis() {
    auto* engine = corePlayer->engine();
    engine->readVisSamples(visL.data(), visR.data(), kVisSamples);
    for (int i = 0; i < kVisSamples; ++i) {
        visMono[i] = 0.5f * (visL[i] + visR[i]);
    }
    const auto& spectrum = analyzer.analyze(visMono);
    Vis::VisFrame frame;
    frame.left = visL;
    frame.right = visR;
    frame.spectrum = spectrum;
    frame.sampleRate = std::max(1, engine->outputSampleRate());
    frame.fftSize = kVisSamples;
    visualizer->update(frame);
}

void MainWindow::tick() {
    bool full = lastFullRepaint.elapsed() >= (visActive ? kFullRepaintMs : 0);
    if (!statusText.isEmpty() && statusAge.elapsed() > kStatusShowMs) {
        statusText.clear();
        full = true;
    }
    if (statusText.isEmpty() && marqueeStep.elapsed() >= kMarqueeStepMs
        && pressedElement != Element::Marquee) {
        marqueeStep.restart();
        ++marqueeOffset;
        full = true;
    }
    if (visActive) {
        updateVis();
    }
    refreshTimer();
    if (full) {
        lastFullRepaint.restart();
        update();
    } else {
        updateSkinRect(Skins::MainWindowSprites::kVisualizer);
    }
}

void MainWindow::changeEvent(QEvent* e) {
    SkinnedWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange) {
        refreshTimer();
        Q_EMIT minimizedChanged(isMinimized());
    }
}

void MainWindow::closeEvent(QCloseEvent* e) {
    // Alt+F4 / window manager close on the main window quits, like Winamp.
    e->accept();
    Q_EMIT closeRequested();
}

// ------------------------------------------------------------------ painting

QString MainWindow::marqueeText() const {
    if (!statusText.isEmpty()) {
        return statusText;
    }
    if (pressedElement == Element::Volume) {
        return QStringLiteral("VOLUME: %1%").arg(volumePercent);
    }
    if (pressedElement == Element::Balance) {
        if (balancePercent == 0) {
            return QStringLiteral("BALANCE: CENTER");
        }
        return QStringLiteral("BALANCE: %1% %2")
            .arg(std::abs(balancePercent))
            .arg(balancePercent < 0 ? "LEFT" : "RIGHT");
    }
    if (pressedElement == Element::Position && seekPreview >= 0) {
        const double dur = corePlayer->durationSeconds();
        return QStringLiteral("SEEK TO: %1/%2 (%3%)")
            .arg(FormatTime(seekPreview * dur), FormatTime(dur))
            .arg(int(seekPreview * 100));
    }
    const auto* t = corePlayer->currentTrack();
    if (!t) {
        return QStringLiteral("QiYaa %1").arg(QApplication::applicationVersion());
    }
    return QStringLiteral("%1. %2 (%3)")
        .arg(corePlayer->currentIndex() + 1)
        .arg(t->displayTitle(), FormatTime(t->durationMs / 1000.0));
}

void MainWindow::drawButton(
    QPainter& p,
    Element e,
    const QPoint& at,
    const QRect& normal,
    const QRect& pressed
) const {
    const bool down = pressedElement == e && pressedInside;
    const TSheet sheet = (e == Element::Options || e == Element::Minimize || e == Element::Shade
                          || e == Element::Close)
        ? TSheet::TitleBar
        : TSheet::CButtons;
    skin().draw(p, sheet, down ? pressed : normal, at);
}

void MainWindow::drawTime(QPainter& p) const {
    const auto st = corePlayer->engine()->state();
    if (st == Audio::AudioEngine::State::Stopped) {
        return;
    }
    if (st == Audio::AudioEngine::State::Paused && (blink.elapsed() / 1000) % 2 == 1) {
        return;
    }

    const double pos = corePlayer->engine()->positionSeconds();
    const double dur = corePlayer->durationSeconds();
    const bool remaining = remainingTimeShown && dur > 0;
    const int secs = remaining ? std::max(0, int(std::ceil(dur - pos))) : int(pos);
    if (remaining) {
        // Minus sign: its own digit-sized cell in nums_ex.bmp, a 5x1 dash in numbers.bmp.
        if (skin().numbersAreExtended()) {
            skin().draw(
                p, TSheet::Numbers, Skins::kMinusSignEx,
                Skins::MainWindowSprites::kTime + QPoint(-1, 0)
            );
        } else {
            skin().draw(
                p, TSheet::Numbers, Skins::kMinusSign,
                Skins::MainWindowSprites::kTime + QPoint(-1, 6)
            );
        }
    }
    const int mm = std::min(secs / 60, 99);
    const int ss = secs % 60;
    const int digits[4] = {mm / 10, mm % 10, ss / 10, ss % 10};
    const int offsets[4] = {9, 21, 39, 51};
    for (int i = 0; i < 4; ++i) {
        skin().draw(
            p, TSheet::Numbers, Skins::DigitSprite(digits[i]),
            Skins::MainWindowSprites::kTime + QPoint(offsets[i], 0)
        );
    }
}

QString MainWindow::miniTimeText() const {
    const auto st = corePlayer->engine()->state();
    if (st == Audio::AudioEngine::State::Stopped) {
        return {};
    }
    if (st == Audio::AudioEngine::State::Paused && (blink.elapsed() / 1000) % 2 == 1) {
        return {};
    }
    const double pos = corePlayer->engine()->positionSeconds();
    const double dur = corePlayer->durationSeconds();
    const bool remaining = remainingTimeShown && dur > 0;
    const int secs = remaining ? std::max(0, int(std::ceil(dur - pos))) : int(pos);
    return QStringLiteral("%1%2:%3")
        .arg(remaining ? QStringLiteral("-") : QString())
        .arg(std::min(secs / 60, 99))
        .arg(secs % 60, 2, 10, QLatin1Char('0'));
}

void MainWindow::paintShaded(QPainter& p) {
    const Skins::Skin& sk = skin();
    sk.draw(
        p, TSheet::TitleBar,
        isActiveWindow() ? Skins::kShadeBackgroundSelected : Skins::kShadeBackground, {0, 0}
    );
    drawButton(
        p, Element::Options, Skins::MainWindowSprites::kOptions, Skins::kOptionsButton,
        Skins::kOptionsButtonDown
    );
    drawButton(
        p, Element::Minimize, Skins::MainWindowSprites::kMinimize, Skins::kMinimizeButton,
        Skins::kMinimizeButtonDown
    );
    const bool shadeDown = pressedElement == Element::Shade && pressedInside;
    sk.draw(
        p, TSheet::TitleBar, shadeDown ? Skins::kShadeButtonShadedDown : Skins::kShadeButtonShaded,
        Skins::MainWindowSprites::kShade
    );
    drawButton(
        p, Element::Close, Skins::MainWindowSprites::kClose, Skins::kCloseButton,
        Skins::kCloseButtonDown
    );

    // Mini time (right-aligned in 5 characters, like Winamp).
    const QString t = miniTimeText().rightJustified(5, u' ');
    sk.drawText(p, {127, 4}, t, 25);

    // Mini position bar.
    const double dur = corePlayer->durationSeconds();
    if (corePlayer->engine()->state() != Audio::AudioEngine::State::Stopped && dur > 0) {
        const double frac = seekPreview >= 0
            ? seekPreview
            : std::clamp(corePlayer->engine()->positionSeconds() / dur, 0.0, 1.0);
        const int x = int(std::lround(frac * (17 - 3)));
        const QRect thumb = x == 0 ? Skins::kShadePositionThumbLeft
            : x >= 14              ? Skins::kShadePositionThumbRight
                                   : Skins::kShadePositionThumb;
        sk.draw(p, TSheet::TitleBar, thumb, QPoint(226 + x, 4));
    }
}

void MainWindow::paintSkin(QPainter& p) {
    if (isShaded()) {
        return paintShaded(p);
    }
    const Skins::Skin& sk = skin();
    const auto st = corePlayer->engine()->state();
    const bool stopped = st == Audio::AudioEngine::State::Stopped;

    sk.draw(p, TSheet::Main, Skins::kMainBackground, {0, 0});
    sk.draw(
        p, TSheet::TitleBar, isActiveWindow() ? Skins::kTitleBarSelected : Skins::kTitleBar, {0, 0}
    );
    drawButton(
        p, Element::Options, Skins::MainWindowSprites::kOptions, Skins::kOptionsButton,
        Skins::kOptionsButtonDown
    );
    drawButton(
        p, Element::Minimize, Skins::MainWindowSprites::kMinimize, Skins::kMinimizeButton,
        Skins::kMinimizeButtonDown
    );
    drawButton(
        p, Element::Shade, Skins::MainWindowSprites::kShade, Skins::kShadeButton,
        Skins::kShadeButtonDown
    );
    drawButton(
        p, Element::Close, Skins::MainWindowSprites::kClose, Skins::kCloseButton,
        Skins::kCloseButtonDown
    );
    sk.draw(p, TSheet::TitleBar, Skins::kClutterBar, Skins::MainWindowSprites::kClutter);

    // Status: play/pause/stop indicator + time.
    const QRect indicator = st == Audio::AudioEngine::State::Paused ? Skins::kPausedIndicator
        : stopped                                                   ? Skins::kStoppedIndicator
                                                                    : Skins::kPlayingIndicator;
    sk.draw(p, TSheet::PlayPaus, indicator, Skins::MainWindowSprites::kPlayPause);
    if (!stopped && st != Audio::AudioEngine::State::Paused) {
        // Little LED: green while playing, red while waiting for data.
        const bool buffering = st == Audio::AudioEngine::State::Buffering;
        sk.draw(
            p, TSheet::PlayPaus, QRect(buffering ? 36 : 39, 0, 3, 9),
            Skins::MainWindowSprites::kPlayPause - QPoint(2, 0)
        );
    }
    drawTime(p);

    // Visualization (drawn over the background, only while playing).
    if (visualizer
        && (st == Audio::AudioEngine::State::Playing || st == Audio::AudioEngine::State::Buffering
        )) {
        p.save();
        p.setClipRect(Skins::MainWindowSprites::kVisualizer);
        visualizer->render(p, Skins::MainWindowSprites::kVisualizer, sk);
        p.restore();
    }

    // Marquee.
    {
        p.save();
        p.setClipRect(Skins::MainWindowSprites::kMarquee);
        QString text = marqueeText();
        const bool scroll = statusText.isEmpty() && pressedElement == Element::None
            && Skins::Skin::TextWidth(text) > Skins::MainWindowSprites::kMarquee.width();
        if (scroll) {
            const QString loop = text + kMarqueeSeparator;
            const int n = int(loop.size());
            const int off = n ? marqueeOffset % n : 0;
            text = loop.mid(off) + loop.left(off) + loop;
        }
        sk.drawText(
            p, Skins::MainWindowSprites::kMarquee.topLeft(), text,
            Skins::MainWindowSprites::kMarquee.width() + Skins::kCharW
        );
        p.restore();
    }

    // kbps / kHz / mono-stereo.
    if (!stopped) {
        const int kbps = corePlayer->currentBitrate();
        const int khz = (corePlayer->engine()->sourceSampleRate() + 500) / 1000;
        if (kbps > 0) {
            const QString t = QString::number(kbps).rightJustified(3, u' ').right(3);
            sk.drawText(p, Skins::MainWindowSprites::kKbps, t);
        }
        if (khz > 0) {
            sk.drawText(
                p, Skins::MainWindowSprites::kKhz,
                QString::number(khz).rightJustified(2, u' ').right(2)
            );
        }
    }
    const int ch = stopped ? 0 : corePlayer->engine()->sourceChannels();
    sk.draw(
        p, TSheet::MonoSter, ch == 1 ? Skins::kMonoSelected : Skins::kMono,
        Skins::MainWindowSprites::kMono
    );
    sk.draw(
        p, TSheet::MonoSter, ch >= 2 ? Skins::kStereoSelected : Skins::kStereo,
        Skins::MainWindowSprites::kStereo
    );

    // Volume.
    {
        const int frame = int(std::lround(volumePercent / 100.0 * 28));
        const int offset = std::max(0, (frame - 1) * Skins::kSliderFrameStep);
        sk.draw(
            p, TSheet::Volume,
            QRect(0, offset, Skins::MainWindowSprites::kVolume.width(), Skins::kSliderFrameH),
            Skins::MainWindowSprites::kVolume.topLeft()
        );
        const int x = int(std::lround(
            volumePercent / 100.0
            * (Skins::MainWindowSprites::kVolume.width() - Skins::kVolumeThumb.width())
        ));
        sk.draw(
            p, TSheet::Volume,
            pressedElement == Element::Volume ? Skins::kVolumeThumbSelected : Skins::kVolumeThumb,
            Skins::MainWindowSprites::kVolume.topLeft() + QPoint(x, 1)
        );
    }
    // Balance.
    {
        const int offset = int(std::abs(balancePercent) / 100.0 * 27) * Skins::kSliderFrameStep;
        sk.draw(
            p, TSheet::Balance,
            QRect(9, offset, Skins::MainWindowSprites::kBalance.width(), Skins::kSliderFrameH),
            Skins::MainWindowSprites::kBalance.topLeft()
        );
        const int x = int(std::lround(
            (balancePercent + 100) / 200.0
            * (Skins::MainWindowSprites::kBalance.width() - Skins::kBalanceThumb.width())
        ));
        sk.draw(
            p, TSheet::Balance,
            pressedElement == Element::Balance ? Skins::kBalanceThumbSelected
                                               : Skins::kBalanceThumb,
            Skins::MainWindowSprites::kBalance.topLeft() + QPoint(x, 1)
        );
    }

    // EQ / PL toggles.
    auto toggle = [&](Element e, const Skins::ToggleSprite& spr, bool on, QPoint at) {
        const bool down = pressedElement == e && pressedInside;
        sk.draw(
            p, TSheet::ShufRep,
            on ? (down ? spr.onPressed : spr.on) : (down ? spr.offPressed : spr.off), at
        );
    };
    toggle(Element::EqToggle, Skins::kEqButton, eqOn, Skins::MainWindowSprites::kEqButton);
    toggle(Element::PlToggle, Skins::kPlButton, plOn, Skins::MainWindowSprites::kPlButton);

    // Position bar.
    sk.draw(
        p, TSheet::PosBar, Skins::kPositionBackground, Skins::MainWindowSprites::kPosition.topLeft()
    );
    const double dur = corePlayer->durationSeconds();
    if (!stopped && dur > 0) {
        const double frac = seekPreview >= 0
            ? seekPreview
            : std::clamp(corePlayer->engine()->positionSeconds() / dur, 0.0, 1.0);
        const int x =
            int(frac * (Skins::MainWindowSprites::kPosition.width() - Skins::kPositionThumb.width())
            );
        sk.draw(
            p, TSheet::PosBar,
            pressedElement == Element::Position ? Skins::kPositionThumbSelected
                                                : Skins::kPositionThumb,
            Skins::MainWindowSprites::kPosition.topLeft() + QPoint(x, 0)
        );
    }

    // Transport.
    drawButton(
        p, Element::Previous, Skins::MainWindowSprites::kPrevious, Skins::kPrevious.normal,
        Skins::kPrevious.pressed
    );
    drawButton(
        p, Element::Play, Skins::MainWindowSprites::kPlay, Skins::kPlay.normal, Skins::kPlay.pressed
    );
    drawButton(
        p, Element::Pause, Skins::MainWindowSprites::kPause, Skins::kPause.normal,
        Skins::kPause.pressed
    );
    drawButton(
        p, Element::Stop, Skins::MainWindowSprites::kStop, Skins::kStop.normal, Skins::kStop.pressed
    );
    drawButton(
        p, Element::Next, Skins::MainWindowSprites::kNext, Skins::kNext.normal, Skins::kNext.pressed
    );
    drawButton(
        p, Element::Eject, Skins::MainWindowSprites::kEject, Skins::kEject.normal,
        Skins::kEject.pressed
    );
    toggle(
        Element::Shuffle, Skins::kShuffle, corePlayer->shuffle(), Skins::MainWindowSprites::kShuffle
    );
    toggle(
        Element::Repeat, Skins::kRepeat, corePlayer->repeat(), Skins::MainWindowSprites::kRepeat
    );
}

// ------------------------------------------------------------------ input

MainWindow::Element MainWindow::hitTestShaded(QPoint p) const {
    struct Area {
        QRect rect;
        Element e;
    };
    static const Area areas[] = {
        {{Skins::MainWindowSprites::kOptions, QSize(9, 9)}, Element::Options},
        {{Skins::MainWindowSprites::kMinimize, QSize(9, 9)}, Element::Minimize},
        {{Skins::MainWindowSprites::kShade, QSize(9, 9)}, Element::Shade},
        {{Skins::MainWindowSprites::kClose, QSize(9, 9)}, Element::Close},
        {{169, 2, 7, 10}, Element::Previous},
        {{176, 2, 10, 10}, Element::Play},
        {{186, 2, 9, 10}, Element::Pause},
        {{195, 2, 9, 10}, Element::Stop},
        {{204, 2, 10, 10}, Element::Next},
        {{215, 2, 10, 10}, Element::Eject},
        {{226, 4, 17, 7}, Element::Position},
        {{127, 4, 25, 6}, Element::Time},
    };
    for (const Area& a : areas) {
        if (Contains(a.rect, p)) {
            return a.e;
        }
    }
    return Element::None;
}

MainWindow::Element MainWindow::hitTest(QPoint p) const {
    if (isShaded()) {
        return hitTestShaded(p);
    }
    struct Area {
        QRect rect;
        Element e;
    };
    static const Area areas[] = {
        {{Skins::MainWindowSprites::kOptions, QSize(9, 9)}, Element::Options},
        {{Skins::MainWindowSprites::kMinimize, QSize(9, 9)}, Element::Minimize},
        {{Skins::MainWindowSprites::kShade, QSize(9, 9)}, Element::Shade},
        {{Skins::MainWindowSprites::kClose, QSize(9, 9)}, Element::Close},
        {{Skins::MainWindowSprites::kPrevious, QSize(23, 18)}, Element::Previous},
        {{Skins::MainWindowSprites::kPlay, QSize(23, 18)}, Element::Play},
        {{Skins::MainWindowSprites::kPause, QSize(23, 18)}, Element::Pause},
        {{Skins::MainWindowSprites::kStop, QSize(23, 18)}, Element::Stop},
        {{Skins::MainWindowSprites::kNext, QSize(22, 18)}, Element::Next},
        {{Skins::MainWindowSprites::kEject, QSize(22, 16)}, Element::Eject},
        {{Skins::MainWindowSprites::kShuffle, QSize(47, 15)}, Element::Shuffle},
        {{Skins::MainWindowSprites::kRepeat, QSize(28, 15)}, Element::Repeat},
        {{Skins::MainWindowSprites::kEqButton, QSize(23, 12)}, Element::EqToggle},
        {{Skins::MainWindowSprites::kPlButton, QSize(23, 12)}, Element::PlToggle},
        {Skins::MainWindowSprites::kVolume, Element::Volume},
        {Skins::MainWindowSprites::kBalance, Element::Balance},
        {Skins::MainWindowSprites::kPosition, Element::Position},
        {Skins::MainWindowSprites::kMarquee.adjusted(0, -3, 0, 3), Element::Marquee},
        {Skins::MainWindowSprites::kVisualizer, Element::Visualizer},
        {{Skins::MainWindowSprites::kTime, QSize(63, 13)}, Element::Time},
    };
    for (const Area& a : areas) {
        if (Contains(a.rect, p)) {
            return a.e;
        }
    }
    return Element::None;
}

bool MainWindow::isDragArea(QPoint p) const {
    // Winamp lets you drag the main window by any spot that isn't a control.
    const Element e = hitTest(p);
    return e == Element::None || e == Element::Marquee;
}

bool MainWindow::skinMousePress(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton) {
        return false;
    }
    const Element e = hitTest(pos);
    if (e == Element::None || e == Element::Marquee) {
        return false;  // marquee drags the window
    }
    if (e == Element::Position
        && corePlayer->engine()->state() == Audio::AudioEngine::State::Stopped) {
        return true;
    }
    pressedElement = e;
    pressedInside = true;
    updateSliderFromMouse(e, pos);
    update();
    return true;
}

void MainWindow::skinMouseMove(QPoint pos) {
    if (pressedElement == Element::None) {
        return;
    }
    if (pressedElement == Element::Volume || pressedElement == Element::Balance
        || pressedElement == Element::Position) {
        updateSliderFromMouse(pressedElement, pos);
    } else {
        pressedInside = hitTest(pos) == pressedElement;
    }
    update();
}

void MainWindow::skinMouseRelease(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton || pressedElement == Element::None) {
        return;
    }
    const Element e = pressedElement;
    const bool inside = hitTest(pos) == e;
    if (e == Element::Position && seekPreview >= 0) {
        corePlayer->seekFraction(seekPreview);
    }
    pressedElement = Element::None;
    seekPreview = -1;
    if (inside && e != Element::Volume && e != Element::Balance && e != Element::Position) {
        activate(e);
    }
    update();
}

void MainWindow::updateSliderFromMouse(Element e, QPoint p) {
    auto fraction = [&](const QRect& r, int thumbW) {
        const double x = p.x() - r.x() - thumbW / 2.0;
        return std::clamp(x / double(r.width() - thumbW), 0.0, 1.0);
    };
    switch (e) {
        case Element::Volume:
            setVolume(int(std::lround(
                fraction(Skins::MainWindowSprites::kVolume, Skins::kVolumeThumb.width()) * 100
            )));
            break;
        case Element::Balance:
            setBalance(int(std::lround(
                fraction(Skins::MainWindowSprites::kBalance, Skins::kBalanceThumb.width()) * 200
                - 100
            )));
            break;
        case Element::Position:
            seekPreview = isShaded()
                ? fraction(QRect(226, 4, 17, 7), 3)
                : fraction(Skins::MainWindowSprites::kPosition, Skins::kPositionThumb.width());
            break;
        default: break;
    }
}

void MainWindow::activate(Element e) {
    switch (e) {
        case Element::Options:
            Q_EMIT menuRequested(globalAt(Skins::MainWindowSprites::kOptions + QPoint(0, 9)));
            break;
        case Element::Minimize: showMinimized(); break;
        case Element::Shade: setShaded(!isShaded()); break;
        case Element::Close: Q_EMIT closeRequested(); break;
        case Element::Previous: corePlayer->previous(); break;
        case Element::Play: corePlayer->play(); break;
        case Element::Pause: corePlayer->pause(); break;
        case Element::Stop: corePlayer->stop(); break;
        case Element::Next: corePlayer->next(); break;
        case Element::Eject:
            Q_EMIT sourcesMenuRequested(globalAt(Skins::MainWindowSprites::kEject + QPoint(0, 16)));
            break;
        case Element::Shuffle: corePlayer->setShuffle(!corePlayer->shuffle()); break;
        case Element::Repeat: corePlayer->setRepeat(!corePlayer->repeat()); break;
        case Element::EqToggle: Q_EMIT eqToggleRequested(); break;
        case Element::PlToggle: Q_EMIT plToggleRequested(); break;
        case Element::Visualizer: setVisMode(VisMode((int(visualizationMode) + 1) % 3)); break;
        case Element::Time: setShowsRemainingTime(!remainingTimeShown); break;
        default: break;
    }
}

void MainWindow::wheelEvent(QWheelEvent* e) {
    const int steps = wheelSteps(e);
    if (steps != 0) {
        setVolume(volumePercent + steps * 4);
        setStatusText(QStringLiteral("VOLUME: %1%").arg(volumePercent));
    }
}

bool MainWindow::skinMouseDoubleClick(QPoint pos, Qt::MouseButton button) {
    // Double click on the title bar toggles shade mode, like Winamp.
    if (button != Qt::LeftButton || pos.y() >= 14 || hitTest(pos) != Element::None) {
        return false;
    }
    setShaded(!isShaded());
    return true;
}

void MainWindow::contextMenuEvent(QContextMenuEvent* e) {
    Q_EMIT menuRequested(e->globalPos());
}

}  // namespace Ui
