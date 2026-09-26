#include "ui/main_window.h"

#include "audio/audio_engine.h"
#include "core/player.h"
#include "skins/skin.h"
#include "skins/sprites.h"

#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QEvent>
#include <QLatin1Char>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace Ui {

using Audio::AudioEngine;
using TSheet = Skins::Skin::Sheet;
using Skins::MainWindowSprites;

namespace {

constexpr int kMarqueeStepMs = 220;
constexpr int kVisFrameMs = 33;
constexpr int kFullRepaintMs = 100;
constexpr int kVisSamples = 1024;
constexpr int kStatusShowMs = 3000;
const QString kMarqueeSeparator = QStringLiteral("  ***  ");

QString FormatTime(double seconds) {
    const int s = std::max(0, int(seconds));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

bool Contains(const QRect& rect, QPoint point) {
    return point.x() >= rect.x() && point.y() >= rect.y() && point.x() < rect.x() + rect.width()
        && point.y() < rect.y() + rect.height();
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

void MainWindow::setVolume(int value) {
    value = std::clamp(value, 0, 100);
    const bool changed = value != volumePercent;
    volumePercent = value;
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

void MainWindow::setBalance(int value) {
    value = std::clamp(value, -100, 100);
    if (std::abs(value) < 8) {
        value = 0;
    }
    const bool changed = value != balancePercent;
    balancePercent = value;
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

void MainWindow::refreshTimer() {
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

void MainWindow::changeEvent(QEvent* event) {
    SkinnedWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        refreshTimer();
        Q_EMIT minimizedChanged(isMinimized());
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    event->accept();
    Q_EMIT closeRequested();
}

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
    QPainter& painter,
    Element element,
    const QPoint& at,
    const QRect& normal,
    const QRect& pressed
) const {
    const bool down = pressedElement == element && pressedInside;
    const TSheet sheet = (element == Element::Options || element == Element::Minimize
                          || element == Element::Shade || element == Element::Close)
        ? TSheet::TitleBar
        : TSheet::CButtons;
    skin().draw(painter, sheet, down ? pressed : normal, at);
}

void MainWindow::drawTime(QPainter& painter) const {
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
                painter, TSheet::Numbers, Skins::kMinusSignEx,
                Skins::MainWindowSprites::kTime + QPoint(-1, 0)
            );
        } else {
            skin().draw(
                painter, TSheet::Numbers, Skins::kMinusSign,
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
            painter, TSheet::Numbers, Skins::DigitSprite(digits[i]),
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

void MainWindow::paintShaded(QPainter& painter) {
    const Skins::Skin& activeSkin = skin();
    activeSkin.draw(
        painter, TSheet::TitleBar,
        isActiveWindow() ? Skins::kShadeBackgroundSelected : Skins::kShadeBackground, {0, 0}
    );
    drawButton(
        painter, Element::Options, Skins::MainWindowSprites::kOptions, Skins::kOptionsButton,
        Skins::kOptionsButtonDown
    );
    drawButton(
        painter, Element::Minimize, Skins::MainWindowSprites::kMinimize, Skins::kMinimizeButton,
        Skins::kMinimizeButtonDown
    );
    const bool shadeDown = pressedElement == Element::Shade && pressedInside;
    activeSkin.draw(
        painter, TSheet::TitleBar,
        shadeDown ? Skins::kShadeButtonShadedDown : Skins::kShadeButtonShaded,
        Skins::MainWindowSprites::kShade
    );
    drawButton(
        painter, Element::Close, Skins::MainWindowSprites::kClose, Skins::kCloseButton,
        Skins::kCloseButtonDown
    );

    const QString t = miniTimeText().rightJustified(5, u' ');
    activeSkin.drawText(painter, {127, 4}, t, 25);

    const double dur = corePlayer->durationSeconds();
    if (corePlayer->engine()->state() != Audio::AudioEngine::State::Stopped && dur > 0) {
        const double frac = seekPreview >= 0
            ? seekPreview
            : std::clamp(corePlayer->engine()->positionSeconds() / dur, 0.0, 1.0);
        const int x = int(std::lround(frac * (17 - 3)));
        const QRect thumb = x == 0 ? Skins::kShadePositionThumbLeft
            : x >= 14              ? Skins::kShadePositionThumbRight
                                   : Skins::kShadePositionThumb;
        activeSkin.draw(painter, TSheet::TitleBar, thumb, QPoint(226 + x, 4));
    }
}

void MainWindow::paintSkin(QPainter& painter) {
    if (isShaded()) {
        return paintShaded(painter);
    }
    const Skins::Skin& activeSkin = skin();
    const auto st = corePlayer->engine()->state();
    const bool stopped = st == Audio::AudioEngine::State::Stopped;

    activeSkin.draw(painter, TSheet::Main, Skins::kMainBackground, {0, 0});
    activeSkin.draw(
        painter, TSheet::TitleBar, isActiveWindow() ? Skins::kTitleBarSelected : Skins::kTitleBar,
        {0, 0}
    );
    drawButton(
        painter, Element::Options, Skins::MainWindowSprites::kOptions, Skins::kOptionsButton,
        Skins::kOptionsButtonDown
    );
    drawButton(
        painter, Element::Minimize, Skins::MainWindowSprites::kMinimize, Skins::kMinimizeButton,
        Skins::kMinimizeButtonDown
    );
    drawButton(
        painter, Element::Shade, Skins::MainWindowSprites::kShade, Skins::kShadeButton,
        Skins::kShadeButtonDown
    );
    drawButton(
        painter, Element::Close, Skins::MainWindowSprites::kClose, Skins::kCloseButton,
        Skins::kCloseButtonDown
    );
    activeSkin.draw(
        painter, TSheet::TitleBar, Skins::kClutterBar, Skins::MainWindowSprites::kClutter
    );

    const QRect indicator = st == Audio::AudioEngine::State::Paused ? Skins::kPausedIndicator
        : stopped                                                   ? Skins::kStoppedIndicator
                                                                    : Skins::kPlayingIndicator;
    activeSkin.draw(painter, TSheet::PlayPaus, indicator, Skins::MainWindowSprites::kPlayPause);
    if (!stopped && st != Audio::AudioEngine::State::Paused) {
        // Little LED: green while playing, red while waiting for data.
        const bool buffering = st == Audio::AudioEngine::State::Buffering;
        activeSkin.draw(
            painter, TSheet::PlayPaus, QRect(buffering ? 36 : 39, 0, 3, 9),
            Skins::MainWindowSprites::kPlayPause - QPoint(2, 0)
        );
    }
    drawTime(painter);

    if (visualizer
        && (st == Audio::AudioEngine::State::Playing || st == Audio::AudioEngine::State::Buffering
        )) {
        painter.save();
        painter.setClipRect(Skins::MainWindowSprites::kVisualizer);
        visualizer->render(painter, Skins::MainWindowSprites::kVisualizer, activeSkin);
        painter.restore();
    }

    {
        painter.save();
        painter.setClipRect(Skins::MainWindowSprites::kMarquee);
        QString text = marqueeText();
        const bool scroll = statusText.isEmpty() && pressedElement == Element::None
            && Skins::Skin::TextWidth(text) > Skins::MainWindowSprites::kMarquee.width();
        if (scroll) {
            const QString loop = text + kMarqueeSeparator;
            const int n = int(loop.size());
            const int off = n ? marqueeOffset % n : 0;
            text = loop.mid(off) + loop.left(off) + loop;
        }
        activeSkin.drawText(
            painter, Skins::MainWindowSprites::kMarquee.topLeft(), text,
            Skins::MainWindowSprites::kMarquee.width() + Skins::kCharWidth
        );
        painter.restore();
    }

    if (!stopped) {
        const int kbps = corePlayer->currentBitrate();
        const int khz = (corePlayer->engine()->sourceSampleRate() + 500) / 1000;
        if (kbps > 0) {
            const QString t = QString::number(kbps).rightJustified(3, u' ').right(3);
            activeSkin.drawText(painter, Skins::MainWindowSprites::kKbps, t);
        }
        if (khz > 0) {
            activeSkin.drawText(
                painter, Skins::MainWindowSprites::kKhz,
                QString::number(khz).rightJustified(2, u' ').right(2)
            );
        }
    }
    const int ch = stopped ? 0 : corePlayer->engine()->sourceChannels();
    activeSkin.draw(
        painter, TSheet::MonoSter, ch == 1 ? Skins::kMonoSelected : Skins::kMono,
        Skins::MainWindowSprites::kMono
    );
    activeSkin.draw(
        painter, TSheet::MonoSter, ch >= 2 ? Skins::kStereoSelected : Skins::kStereo,
        Skins::MainWindowSprites::kStereo
    );

    {
        const int frame = int(std::lround(volumePercent / 100.0 * 28));
        const int offset = std::max(0, (frame - 1) * Skins::kSliderFrameStep);
        activeSkin.draw(
            painter, TSheet::Volume,
            QRect(0, offset, Skins::MainWindowSprites::kVolume.width(), Skins::kSliderFrameHeight),
            Skins::MainWindowSprites::kVolume.topLeft()
        );
        const int x = int(std::lround(
            volumePercent / 100.0
            * (Skins::MainWindowSprites::kVolume.width() - Skins::kVolumeThumb.width())
        ));
        activeSkin.draw(
            painter, TSheet::Volume,
            pressedElement == Element::Volume ? Skins::kVolumeThumbSelected : Skins::kVolumeThumb,
            Skins::MainWindowSprites::kVolume.topLeft() + QPoint(x, 1)
        );
    }
    {
        const int offset = int(std::abs(balancePercent) / 100.0 * 27) * Skins::kSliderFrameStep;
        activeSkin.draw(
            painter, TSheet::Balance,
            QRect(9, offset, Skins::MainWindowSprites::kBalance.width(), Skins::kSliderFrameHeight),
            Skins::MainWindowSprites::kBalance.topLeft()
        );
        const int x = int(std::lround(
            (balancePercent + 100) / 200.0
            * (Skins::MainWindowSprites::kBalance.width() - Skins::kBalanceThumb.width())
        ));
        activeSkin.draw(
            painter, TSheet::Balance,
            pressedElement == Element::Balance ? Skins::kBalanceThumbSelected
                                               : Skins::kBalanceThumb,
            Skins::MainWindowSprites::kBalance.topLeft() + QPoint(x, 1)
        );
    }

    auto toggle = [&](Element element, const Skins::ToggleSprite& spr, bool on, QPoint at) {
        const bool down = pressedElement == element && pressedInside;
        activeSkin.draw(
            painter, TSheet::ShufRep,
            on ? (down ? spr.onPressed : spr.on) : (down ? spr.offPressed : spr.off), at
        );
    };
    toggle(Element::EqToggle, Skins::kEqButton, eqOn, Skins::MainWindowSprites::kEqButton);
    toggle(Element::PlToggle, Skins::kPlButton, plOn, Skins::MainWindowSprites::kPlButton);

    activeSkin.draw(
        painter, TSheet::PosBar, Skins::kPositionBackground,
        Skins::MainWindowSprites::kPosition.topLeft()
    );
    const double dur = corePlayer->durationSeconds();
    if (!stopped && dur > 0) {
        const double frac = seekPreview >= 0
            ? seekPreview
            : std::clamp(corePlayer->engine()->positionSeconds() / dur, 0.0, 1.0);
        const int x =
            int(frac * (Skins::MainWindowSprites::kPosition.width() - Skins::kPositionThumb.width())
            );
        activeSkin.draw(
            painter, TSheet::PosBar,
            pressedElement == Element::Position ? Skins::kPositionThumbSelected
                                                : Skins::kPositionThumb,
            Skins::MainWindowSprites::kPosition.topLeft() + QPoint(x, 0)
        );
    }

    drawButton(
        painter, Element::Previous, Skins::MainWindowSprites::kPrevious, Skins::kPrevious.normal,
        Skins::kPrevious.pressed
    );
    drawButton(
        painter, Element::Play, Skins::MainWindowSprites::kPlay, Skins::kPlay.normal,
        Skins::kPlay.pressed
    );
    drawButton(
        painter, Element::Pause, Skins::MainWindowSprites::kPause, Skins::kPause.normal,
        Skins::kPause.pressed
    );
    drawButton(
        painter, Element::Stop, Skins::MainWindowSprites::kStop, Skins::kStop.normal,
        Skins::kStop.pressed
    );
    drawButton(
        painter, Element::Next, Skins::MainWindowSprites::kNext, Skins::kNext.normal,
        Skins::kNext.pressed
    );
    drawButton(
        painter, Element::Eject, Skins::MainWindowSprites::kEject, Skins::kEject.normal,
        Skins::kEject.pressed
    );
    toggle(
        Element::Shuffle, Skins::kShuffle, corePlayer->shuffle(), Skins::MainWindowSprites::kShuffle
    );
    toggle(
        Element::Repeat, Skins::kRepeat, corePlayer->repeat(), Skins::MainWindowSprites::kRepeat
    );
}

MainWindow::Element MainWindow::hitTestShaded(QPoint point) const {
    struct Area {
        QRect rect;
        Element element;
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
    for (const Area& area : areas) {
        if (Contains(area.rect, point)) {
            return area.element;
        }
    }
    return Element::None;
}

MainWindow::Element MainWindow::hitTest(QPoint point) const {
    if (isShaded()) {
        return hitTestShaded(point);
    }
    struct Area {
        QRect rect;
        Element element;
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
    for (const Area& area : areas) {
        if (Contains(area.rect, point)) {
            return area.element;
        }
    }
    return Element::None;
}

bool MainWindow::isDragArea(QPoint point) const {
    const Element element = hitTest(point);
    return element == Element::None || element == Element::Marquee;
}

bool MainWindow::skinMousePress(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton) {
        return false;
    }
    const Element element = hitTest(pos);
    if (element == Element::None || element == Element::Marquee) {
        return false;
    }
    if (element == Element::Position
        && corePlayer->engine()->state() == Audio::AudioEngine::State::Stopped) {
        return true;
    }
    pressedElement = element;
    pressedInside = true;
    updateSliderFromMouse(element, pos);
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
    const Element element = pressedElement;
    const bool inside = hitTest(pos) == element;
    if (element == Element::Position && seekPreview >= 0) {
        corePlayer->seekFraction(seekPreview);
    }
    pressedElement = Element::None;
    seekPreview = -1;
    if (inside && element != Element::Volume && element != Element::Balance
        && element != Element::Position) {
        activate(element);
    }
    update();
}

void MainWindow::updateSliderFromMouse(Element element, QPoint point) {
    auto fraction = [&](const QRect& rect, int thumbW) {
        const double x = point.x() - rect.x() - thumbW / 2.0;
        return std::clamp(x / double(rect.width() - thumbW), 0.0, 1.0);
    };
    switch (element) {
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

void MainWindow::activate(Element element) {
    switch (element) {
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

void MainWindow::wheelEvent(QWheelEvent* event) {
    const int steps = wheelSteps(event);
    if (steps != 0) {
        setVolume(volumePercent + steps * 4);
        setStatusText(QStringLiteral("VOLUME: %1%").arg(volumePercent));
    }
}

bool MainWindow::skinMouseDoubleClick(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton || pos.y() >= 14 || hitTest(pos) != Element::None) {
        return false;
    }
    setShaded(!isShaded());
    return true;
}

void MainWindow::contextMenuEvent(QContextMenuEvent* event) {
    Q_EMIT menuRequested(event->globalPos());
}

}  // namespace Ui
