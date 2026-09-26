#include "ui/equalizer_window.h"

#include "audio/eq_presets.h"
#include "skins/skin.h"
#include "skins/sprites.h"

#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace Ui {

using Audio::EqSettings;
using Audio::kEqBandHz;
using Audio::kEqBands;
using Audio::kEqMaxDb;
using TSheet = Skins::Skin::Sheet;
using Skins::EqualizerSprites;

namespace {

constexpr int kElNone = 0, kElClose = 1, kElOn = 2, kElAuto = 3, kElPresets = 4, kElPreamp = 5,
              kElBand0 = 6;
constexpr int kElShade = 20, kElShadeVolume = 21, kElShadeBalance = 22;
constexpr QPoint kShadeButton{254, 3};
using Skins::EqualizerShadeSprites;

bool Contains(const QRect& rect, QPoint point) {
    return point.x() >= rect.x() && point.y() >= rect.y() && point.x() < rect.x() + rect.width()
        && point.y() < rect.y() + rect.height();
}

QRect SliderRect(int element) {
    const int x = element == kElPreamp ? Skins::EqualizerSprites::kPreampPos.x()
                                       : Skins::EqualizerSprites::kBandsX
            + (element - kElBand0) * Skins::EqualizerSprites::kBandStep;
    return {
        x, Skins::EqualizerSprites::kSlidersY, Skins::EqualizerSprites::kSliderSize.width(),
        Skins::EqualizerSprites::kSliderSize.height()
    };
}

QString BandName(int band) {
    const double hz = Audio::kEqBandHz[band];
    return hz >= 1000 ? QStringLiteral("%1KHZ").arg(hz / 1000) : QStringLiteral("%1HZ").arg(hz);
}

// Natural cubic spline through (xs, ys), sampled at every integer x in [0, xs.back()].
// Port of webamp's spline.js (itself adapted from morganherlocker/cubic-spline, MIT).
QList<double> NaturalSpline(const QList<double>& xs, const QList<double>& ys) {
    const int n = int(xs.size()) - 1;
    // Tridiagonal system for the slopes k.
    QList<double> a(n + 1), b(n + 1), c(n + 1), d(n + 1), k(n + 1);
    for (int i = 0; i <= n; ++i) {
        if (i == 0) {
            const double h = xs[1] - xs[0];
            b[i] = 2 / h;
            c[i] = 1 / h;
            d[i] = 3 * (ys[1] - ys[0]) / (h * h);
        } else if (i == n) {
            const double h = xs[n] - xs[n - 1];
            a[i] = 1 / h;
            b[i] = 2 / h;
            d[i] = 3 * (ys[n] - ys[n - 1]) / (h * h);
        } else {
            const double h0 = xs[i] - xs[i - 1], h1 = xs[i + 1] - xs[i];
            a[i] = 1 / h0;
            b[i] = 2 * (1 / h0 + 1 / h1);
            c[i] = 1 / h1;
            d[i] = 3 * ((ys[i] - ys[i - 1]) / (h0 * h0) + (ys[i + 1] - ys[i]) / (h1 * h1));
        }
    }
    // Thomas algorithm.
    for (int i = 1; i <= n; ++i) {
        const double m = a[i] / b[i - 1];
        b[i] -= m * c[i - 1];
        d[i] -= m * d[i - 1];
    }
    k[n] = d[n] / b[n];
    for (int i = n - 1; i >= 0; --i) {
        k[i] = (d[i] - c[i] * k[i + 1]) / b[i];
    }

    QList<double> out;
    int i = 1;
    for (int x = 0; x <= int(xs[n]); ++x) {
        while (i < n && xs[i] < x) {
            ++i;
        }
        const double h = xs[i] - xs[i - 1];
        const double t = (x - xs[i - 1]) / h;
        const double aa = k[i - 1] * h - (ys[i] - ys[i - 1]);
        const double bb = -k[i] * h + (ys[i] - ys[i - 1]);
        out << (1 - t) * ys[i - 1] + t * ys[i] + t * (1 - t) * (aa * (1 - t) + bb * t);
    }
    return out;
}

constexpr int kGraphH = 19;

double DbToGraphY(double db) {
    return (1.0 - (db + Audio::kEqMaxDb) / (2 * Audio::kEqMaxDb)) * (kGraphH - 1);
}

}  // namespace

EqualizerWindow::EqualizerWindow(const Skins::Skin* skin, QWidget* parent)
    : SkinnedWindow(skin, Skins::EqualizerSprites::kSize, parent) {
    setWindowTitle(QStringLiteral("QiYaa Equalizer"));
}

void EqualizerWindow::setSettings(const Audio::EqSettings& settings) {
    equalizerSettings = settings;
    update();
}

QList<double> EqualizerWindow::GraphCurve(const Audio::EqSettings& settings) {
    QList<double> xs, ys;
    for (int i = 0; i < Audio::kEqBands; ++i) {
        xs << i * 12.0;
        ys << DbToGraphY(settings.bandsDb[i]);
    }
    return NaturalSpline(xs, ys);
}

// ------------------------------------------------------------------ painting

void EqualizerWindow::drawSlider(QPainter& painter, QPoint at, double db, bool active) const {
    const int frame = int(std::lround((db + Audio::kEqMaxDb) / (2 * Audio::kEqMaxDb) * 27));
    const QRect src(
        Skins::EqualizerSprites::kSliderFrames.x() + (frame % 14) * 15,
        Skins::EqualizerSprites::kSliderFrames.y() + (frame / 14) * 65,
        Skins::EqualizerSprites::kSliderSize.width(), Skins::EqualizerSprites::kSliderSize.height()
    );
    skin().draw(painter, TSheet::EqMain, src, at);
    const int thumbY = int(std::lround(
        (1.0 - (db + Audio::kEqMaxDb) / (2 * Audio::kEqMaxDb))
        * Skins::EqualizerSprites::kSliderTravel
    ));
    skin().draw(
        painter, TSheet::EqMain,
        active ? Skins::EqualizerSprites::kThumbSelected : Skins::EqualizerSprites::kThumb,
        at + QPoint(1, thumbY)
    );
}

void EqualizerWindow::drawGraph(QPainter& painter) const {
    const QPoint origin = Skins::EqualizerSprites::kGraphPos;
    skin().draw(painter, TSheet::EqMain, Skins::EqualizerSprites::kGraphBackground, origin);
    skin().draw(
        painter, TSheet::EqMain, Skins::EqualizerSprites::kPreampLine,
        origin + QPoint(0, int(std::lround(DbToGraphY(equalizerSettings.preampDb))))
    );

    const QImage& sheet = skin().sheet(TSheet::EqMain);
    const QList<double> ys = GraphCurve(equalizerSettings);
    int lastY = int(std::lround(ys.first()));
    for (int x = 0; x < ys.size(); ++x) {
        const int y = std::clamp(int(std::lround(ys[x])), 0, kGraphH - 1);
        const int top = std::min(y, lastY), bottom = std::max(y, lastY);
        for (int yy = top; yy <= bottom; ++yy) {
            // Colour depends on height, taken from the 1px column in the skin.
            const QColor c = sheet.isNull() ? QColor(Qt::green)
                                            : QColor::fromRgb(sheet.pixel(
                                                  Skins::EqualizerSprites::kGraphLineColors.x(),
                                                  Skins::EqualizerSprites::kGraphLineColors.y() + yy
                                              ));
            painter.fillRect(origin.x() + 2 + x, origin.y() + yy, 1, 1, c);
        }
        lastY = y;
    }
}

void EqualizerWindow::setShaded(bool shaded) {
    if (shaded == isShaded()) {
        return;
    }
    applyShade(shaded, shaded ? QSize(275, 14) : Skins::EqualizerSprites::kSize);
    update();
}

void EqualizerWindow::setMixer(int volume, int balance) {
    volumePercent = volume;
    balancePercent = balance;
    if (isShaded()) {
        update();
    }
}

void EqualizerWindow::paintShaded(QPainter& painter) {
    const Skins::Skin& activeSkin = skin();
    activeSkin.draw(
        painter, TSheet::EqEx,
        isActiveWindow() ? Skins::EqualizerShadeSprites::kShadeBackgroundSelected
                         : Skins::EqualizerShadeSprites::kShadeBackground,
        {0, 0}
    );
    // Thumb sprite changes with the value: left / centre / right third.
    const int vThird = std::clamp(volumePercent * 3 / 101, 0, 2);
    const int vx =
        Skins::EqualizerShadeSprites::kVolume.x()
        + int(
            std::lround(volumePercent / 100.0 * (Skins::EqualizerShadeSprites::kVolume.width() - 3))
        );
    activeSkin.draw(
        painter, TSheet::EqEx, Skins::EqualizerShadeSprites::kVolumeThumb[vThird],
        {vx, Skins::EqualizerShadeSprites::kVolume.y()}
    );
    const int bThird = std::clamp((balancePercent + 100) * 3 / 201, 0, 2);
    const int bx = Skins::EqualizerShadeSprites::kBalance.x()
        + int(std::lround(
            (balancePercent + 100) / 200.0 * (Skins::EqualizerShadeSprites::kBalance.width() - 3)
        ));
    activeSkin.draw(
        painter, TSheet::EqEx, Skins::EqualizerShadeSprites::kBalanceThumb[bThird],
        {bx, Skins::EqualizerShadeSprites::kBalance.y()}
    );
    if (pressedElement == kElShade && pressedInside) {
        activeSkin.draw(
            painter, TSheet::EqEx, Skins::EqualizerShadeSprites::kShadeButtonShadedDown,
            kShadeButton
        );
    }
    if (pressedElement == kElClose && pressedInside) {
        activeSkin.draw(
            painter, TSheet::EqEx, Skins::EqualizerShadeSprites::kCloseButtonDown,
            Skins::EqualizerSprites::kClose
        );
    }
}

void EqualizerWindow::paintSkin(QPainter& painter) {
    if (isShaded()) {
        return paintShaded(painter);
    }
    const Skins::Skin& activeSkin = skin();
    activeSkin.draw(painter, TSheet::EqMain, Skins::EqualizerSprites::kBackground, {0, 0});
    activeSkin.draw(
        painter, TSheet::EqMain,
        isActiveWindow() ? Skins::EqualizerSprites::kTitleBarSelected
                         : Skins::EqualizerSprites::kTitleBar,
        {0, 0}
    );
    if (pressedElement == kElClose && pressedInside) {
        activeSkin.draw(
            painter, TSheet::EqMain, Skins::EqualizerSprites::kCloseButtonDown,
            Skins::EqualizerSprites::kClose
        );
    }
    if (pressedElement == kElShade && pressedInside) {
        activeSkin.draw(
            painter, TSheet::EqEx, Skins::EqualizerShadeSprites::kShadeButtonDown, kShadeButton
        );
    }

    auto toggle = [&](int element, const Skins::ToggleSprite& spr, bool on, QPoint at) {
        const bool down = pressedElement == element && pressedInside;
        activeSkin.draw(
            painter, TSheet::EqMain,
            on ? (down ? spr.onPressed : spr.on) : (down ? spr.offPressed : spr.off), at
        );
    };
    toggle(
        kElOn, Skins::EqualizerSprites::kOn, equalizerSettings.enabled,
        Skins::EqualizerSprites::kOnPos
    );
    toggle(kElAuto, Skins::EqualizerSprites::kAuto, autoEnabled, Skins::EqualizerSprites::kAutoPos);
    activeSkin.draw(
        painter, TSheet::EqMain,
        pressedElement == kElPresets && pressedInside
            ? Skins::EqualizerSprites::kPresetsButtonSelected
            : Skins::EqualizerSprites::kPresetsButton,
        Skins::EqualizerSprites::kPresetsPos
    );

    drawGraph(painter);
    drawSlider(
        painter, SliderRect(kElPreamp).topLeft(), equalizerSettings.preampDb,
        pressedElement == kElPreamp
    );
    for (int i = 0; i < Audio::kEqBands; ++i) {
        drawSlider(
            painter, SliderRect(kElBand0 + i).topLeft(), equalizerSettings.bandsDb[i],
            pressedElement == kElBand0 + i
        );
    }
}

// ------------------------------------------------------------------ input

int EqualizerWindow::hitTest(QPoint point) const {
    if (Contains({Skins::EqualizerSprites::kClose, QSize(9, 9)}, point)) {
        return kElClose;
    }
    if (Contains({kShadeButton, QSize(9, 9)}, point)) {
        return kElShade;
    }
    if (isShaded()) {
        if (Contains(Skins::EqualizerShadeSprites::kVolume, point)) {
            return kElShadeVolume;
        }
        if (Contains(Skins::EqualizerShadeSprites::kBalance, point)) {
            return kElShadeBalance;
        }
        return kElNone;
    }
    if (Contains({Skins::EqualizerSprites::kOnPos, QSize(26, 12)}, point)) {
        return kElOn;
    }
    if (Contains({Skins::EqualizerSprites::kAutoPos, QSize(32, 12)}, point)) {
        return kElAuto;
    }
    if (Contains({Skins::EqualizerSprites::kPresetsPos, QSize(44, 12)}, point)) {
        return kElPresets;
    }
    if (Contains(SliderRect(kElPreamp), point)) {
        return kElPreamp;
    }
    for (int i = 0; i < Audio::kEqBands; ++i) {
        if (Contains(SliderRect(kElBand0 + i), point)) {
            return kElBand0 + i;
        }
    }
    return kElNone;
}

bool EqualizerWindow::isDragArea(QPoint point) const {
    return hitTest(point) == kElNone;
}

double* EqualizerWindow::valueFor(int element) {
    if (element == kElPreamp) {
        return &equalizerSettings.preampDb;
    }
    if (element >= kElBand0 && element < kElBand0 + Audio::kEqBands) {
        return &equalizerSettings.bandsDb[element - kElBand0];
    }
    return nullptr;
}

void EqualizerWindow::changed(int element) {
    Q_EMIT settingsChanged(equalizerSettings);
    if (const double* v = valueFor(element)) {
        const QString name =
            element == kElPreamp ? QStringLiteral("PREAMP") : BandName(element - kElBand0);
        Q_EMIT statusText(QStringLiteral("EQ: %1 %2%3 DB")
                              .arg(name, *v >= 0 ? QStringLiteral("+") : QString())
                              .arg(*v, 0, 'f', 1));
    }
    update();
}

void EqualizerWindow::setFromMouse(int element, QPoint point) {
    if (element == kElShadeVolume || element == kElShadeBalance) {
        const QRect rect = element == kElShadeVolume ? Skins::EqualizerShadeSprites::kVolume
                                                     : Skins::EqualizerShadeSprites::kBalance;
        const double frac = std::clamp((point.x() - rect.x() - 1.5) / (rect.width() - 3), 0.0, 1.0);
        if (element == kElShadeVolume) {
            Q_EMIT volumeRequested(int(std::lround(frac * 100)));
        } else {
            Q_EMIT balanceRequested(int(std::lround(frac * 200 - 100)));
        }
        return;
    }
    double* v = valueFor(element);
    if (!v) {
        return;
    }
    const QRect rect = SliderRect(element);
    const double top = std::clamp(
        double(point.y() - rect.y()) - 5.5, 0.0, double(Skins::EqualizerSprites::kSliderTravel)
    );
    double db =
        Audio::kEqMaxDb - top / Skins::EqualizerSprites::kSliderTravel * 2 * Audio::kEqMaxDb;
    db = std::round(db * 10) / 10;
    if (std::abs(db) < 0.6) {
        db = 0;  // centre detent
    }
    if (db == *v) {
        return;
    }
    *v = db;
    changed(element);
}

bool EqualizerWindow::skinMousePress(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton) {
        return false;
    }
    const int el = hitTest(pos);
    if (el == kElNone) {
        return false;
    }
    pressedElement = el;
    pressedInside = true;
    setFromMouse(el, pos);
    update();
    return true;
}

void EqualizerWindow::skinMouseMove(QPoint pos) {
    if (pressedElement == kElNone) {
        return;
    }
    if (valueFor(pressedElement) || pressedElement == kElShadeVolume
        || pressedElement == kElShadeBalance) {
        setFromMouse(pressedElement, pos);
    } else {
        pressedInside = hitTest(pos) == pressedElement;
    }
    update();
}

void EqualizerWindow::skinMouseRelease(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton || pressedElement == kElNone) {
        return;
    }
    const int el = pressedElement;
    const bool inside = hitTest(pos) == el;
    pressedElement = kElNone;
    update();
    if (!inside) {
        return;
    }
    switch (el) {
        case kElClose: Q_EMIT closeRequested(); break;
        case kElShade: setShaded(!isShaded()); break;
        case kElOn:
            equalizerSettings.enabled = !equalizerSettings.enabled;
            Q_EMIT settingsChanged(equalizerSettings);
            Q_EMIT statusText(
                equalizerSettings.enabled ? QStringLiteral("EQ: ON") : QStringLiteral("EQ: OFF")
            );
            break;
        case kElAuto: autoEnabled = !autoEnabled; break;
        case kElPresets: showPresets(); break;
        default: break;
    }
}

bool EqualizerWindow::skinMouseDoubleClick(QPoint pos, Qt::MouseButton button) {
    if (button == Qt::LeftButton && pos.y() < 14 && hitTest(pos) == kElNone) {  // title bar
        setShaded(!isShaded());
        return true;
    }
    // Double click on a slider resets it to 0 dB.
    const int el = hitTest(pos);
    double* v = valueFor(el);
    if (button != Qt::LeftButton || !v) {
        return false;
    }
    *v = 0;
    changed(el);
    return true;
}

void EqualizerWindow::wheelEvent(QWheelEvent* event) {
    const int el = hitTest(toSkin(event->position()));
    double* v = valueFor(el);
    const int steps = wheelSteps(event);
    if (!v || steps == 0) {
        return;
    }
    *v = std::clamp(std::round((*v + steps * 0.5) * 10) / 10, -Audio::kEqMaxDb, Audio::kEqMaxDb);
    changed(el);
}

void EqualizerWindow::applyPreset(const Audio::EqPreset& preset) {
    const bool enabled = equalizerSettings.enabled;
    equalizerSettings = preset.settings;
    equalizerSettings.enabled = enabled;
    Q_EMIT settingsChanged(equalizerSettings);
    Q_EMIT statusText(QStringLiteral("EQ: ") + preset.name.toUpper());
    update();
}

void EqualizerWindow::loadEqf() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Пресет эквалайзера"), QDir::homePath(),
        QStringLiteral("Пресеты Winamp (*.eqf *.EQF *.q1)")
    );
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    QList<Audio::EqPreset> presets;
    if (!file.open(QIODevice::ReadOnly) || !Audio::ParseEqf(file.readAll(), &presets)) {
        Q_EMIT statusText(QStringLiteral("EQ: не удалось прочитать файл"));
        return;
    }
    if (presets.size() == 1) {
        return applyPreset(presets.first());
    }
    // Libraries (.q1) hold many presets: let the user pick one.
    QStringList names;
    for (const auto& p : presets) {
        names << p.name;
    }
    bool ok = false;
    const QString name = QInputDialog::getItem(
        this, QStringLiteral("Пресет"), QStringLiteral("Выберите пресет:"), names, 0, false, &ok
    );
    if (ok) {
        applyPreset(presets.value(names.indexOf(name)));
    }
}

void EqualizerWindow::saveEqf() {
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Сохранить пресет"), QStringLiteral("Название:"), QLineEdit::Normal,
        QStringLiteral("QiYaa"), &ok
    );
    if (!ok || name.isEmpty()) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Сохранить пресет"),
        QDir::homePath() + u'/' + name + QStringLiteral(".eqf"),
        QStringLiteral("Пресет Winamp (*.eqf)")
    );
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)
        && file.write(Audio::WriteEqf({{name, equalizerSettings}})) > 0) {
        Q_EMIT statusText(QStringLiteral("EQ: сохранено"));
    } else {
        Q_EMIT statusText(QStringLiteral("EQ: не удалось сохранить"));
    }
}

void EqualizerWindow::showPresets() {
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction(QStringLiteral("Сбросить (0 дБ)"), this, [this] {
        const bool enabled = equalizerSettings.enabled;
        equalizerSettings = Audio::EqSettings{};
        equalizerSettings.enabled = enabled;
        Q_EMIT settingsChanged(equalizerSettings);
        update();
    });
    menu->addAction(QStringLiteral("Загрузить .eqf..."), this, &EqualizerWindow::loadEqf);
    menu->addAction(QStringLiteral("Сохранить в .eqf..."), this, &EqualizerWindow::saveEqf);
    menu->addSeparator();
    for (const Audio::EqPreset& preset : Audio::BuiltinEqPresets()) {
        menu->addAction(preset.name, this, [this, preset] { applyPreset(preset); });
    }
    const QPoint at(
        Skins::EqualizerSprites::kPresetsPos.x(), Skins::EqualizerSprites::kPresetsPos.y() + 12
    );
    menu->popup(mapToGlobal(QPoint(qRound(at.x() * scale()), qRound(at.y() * scale()))));
}

void EqualizerWindow::closeEvent(QCloseEvent* event) {
    // Window manager close: just hide (the owner keeps the EQ/PL buttons in sync).
    event->ignore();
    Q_EMIT closeRequested();
}

}  // namespace Ui
