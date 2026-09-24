#include "ui/EqualizerWindow.h"

#include <algorithm>
#include <cmath>

#include <QCloseEvent>
#include <QMenu>
#include <QPainter>
#include <QWheelEvent>

#include "audio/EqPresets.h"
#include "skin/Skin.h"
#include "skin/SkinSprites.h"

namespace qiyaa {

using audio::EqSettings;
using audio::kEqBandHz;
using audio::kEqBands;
using audio::kEqMaxDb;
using Sheet = Skin::Sheet;
namespace E = sprites::eq;

namespace {

constexpr int kElNone = 0, kElClose = 1, kElOn = 2, kElAuto = 3, kElPresets = 4, kElPreamp = 5, kElBand0 = 6;

bool contains(const QRect& r, QPoint p) {
    return p.x() >= r.x() && p.y() >= r.y() && p.x() < r.x() + r.width() && p.y() < r.y() + r.height();
}

QRect sliderRect(int element) {
    const int x = element == kElPreamp ? E::kPreampPos.x() : E::kBandsX + (element - kElBand0) * E::kBandStep;
    return {x, E::kSlidersY, E::kSliderSize.width(), E::kSliderSize.height()};
}

QString bandName(int band) {
    const double hz = kEqBandHz[band];
    return hz >= 1000 ? QStringLiteral("%1KHZ").arg(hz / 1000) : QStringLiteral("%1HZ").arg(hz);
}

// Natural cubic spline through (xs, ys), sampled at every integer x in [0, xs.back()].
// Port of webamp's spline.js (itself adapted from morganherlocker/cubic-spline, MIT).
QList<double> naturalSpline(const QList<double>& xs, const QList<double>& ys) {
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
    for (int i = n - 1; i >= 0; --i) k[i] = (d[i] - c[i] * k[i + 1]) / b[i];

    QList<double> out;
    int i = 1;
    for (int x = 0; x <= int(xs[n]); ++x) {
        while (i < n && xs[i] < x) ++i;
        const double h = xs[i] - xs[i - 1];
        const double t = (x - xs[i - 1]) / h;
        const double aa = k[i - 1] * h - (ys[i] - ys[i - 1]);
        const double bb = -k[i] * h + (ys[i] - ys[i - 1]);
        out << (1 - t) * ys[i - 1] + t * ys[i] + t * (1 - t) * (aa * (1 - t) + bb * t);
    }
    return out;
}

constexpr int kGraphH = 19;

double dbToGraphY(double db) {
    return (1.0 - (db + kEqMaxDb) / (2 * kEqMaxDb)) * (kGraphH - 1);
}

}  // namespace

EqualizerWindow::EqualizerWindow(const Skin* skin, QWidget* parent) : SkinnedWindow(skin, E::kSize, parent) {
    setWindowTitle(QStringLiteral("QiYaa Equalizer"));
}

void EqualizerWindow::setSettings(const EqSettings& s) {
    m_settings = s;
    update();
}

QList<double> EqualizerWindow::graphCurve(const EqSettings& s) {
    QList<double> xs, ys;
    for (int i = 0; i < kEqBands; ++i) {
        xs << i * 12.0;
        ys << dbToGraphY(s.bandsDb[i]);
    }
    return naturalSpline(xs, ys);
}

// ------------------------------------------------------------------ painting

void EqualizerWindow::drawSlider(QPainter& p, QPoint at, double db, bool active) const {
    const int frame = int(std::lround((db + kEqMaxDb) / (2 * kEqMaxDb) * 27));
    const QRect src(E::kSliderFrames.x() + (frame % 14) * 15, E::kSliderFrames.y() + (frame / 14) * 65,
                    E::kSliderSize.width(), E::kSliderSize.height());
    skin().draw(p, Sheet::EqMain, src, at);
    const int thumbY = int(std::lround((1.0 - (db + kEqMaxDb) / (2 * kEqMaxDb)) * E::kSliderTravel));
    skin().draw(p, Sheet::EqMain, active ? E::kThumbSelected : E::kThumb, at + QPoint(1, thumbY));
}

void EqualizerWindow::drawGraph(QPainter& p) const {
    const QPoint origin = E::kGraphPos;
    skin().draw(p, Sheet::EqMain, E::kGraphBackground, origin);
    skin().draw(p, Sheet::EqMain, E::kPreampLine, origin + QPoint(0, int(std::lround(dbToGraphY(m_settings.preampDb)))));

    const QImage& sheet = skin().sheet(Sheet::EqMain);
    const QList<double> ys = graphCurve(m_settings);
    int lastY = int(std::lround(ys.first()));
    for (int x = 0; x < ys.size(); ++x) {
        const int y = std::clamp(int(std::lround(ys[x])), 0, kGraphH - 1);
        const int top = std::min(y, lastY), bottom = std::max(y, lastY);
        for (int yy = top; yy <= bottom; ++yy) {
            // Colour depends on height, taken from the 1px column in the skin.
            const QColor c = sheet.isNull() ? QColor(Qt::green)
                                            : QColor::fromRgb(sheet.pixel(E::kGraphLineColors.x(), E::kGraphLineColors.y() + yy));
            p.fillRect(origin.x() + 2 + x, origin.y() + yy, 1, 1, c);
        }
        lastY = y;
    }
}

void EqualizerWindow::paintSkin(QPainter& p) {
    const Skin& sk = skin();
    sk.draw(p, Sheet::EqMain, E::kBackground, {0, 0});
    sk.draw(p, Sheet::EqMain, isActiveWindow() ? E::kTitleBarSelected : E::kTitleBar, {0, 0});
    if (m_pressed == kElClose && m_pressedInside) sk.draw(p, Sheet::EqMain, E::kCloseButtonDown, E::kClose);

    auto toggle = [&](int el, const sprites::ToggleSprite& spr, bool on, QPoint at) {
        const bool down = m_pressed == el && m_pressedInside;
        sk.draw(p, Sheet::EqMain, on ? (down ? spr.onPressed : spr.on) : (down ? spr.offPressed : spr.off), at);
    };
    toggle(kElOn, E::kOn, m_settings.enabled, E::kOnPos);
    toggle(kElAuto, E::kAuto, m_auto, E::kAutoPos);
    sk.draw(p, Sheet::EqMain, m_pressed == kElPresets && m_pressedInside ? E::kPresetsButtonSelected : E::kPresetsButton,
            E::kPresetsPos);

    drawGraph(p);
    drawSlider(p, sliderRect(kElPreamp).topLeft(), m_settings.preampDb, m_pressed == kElPreamp);
    for (int i = 0; i < kEqBands; ++i)
        drawSlider(p, sliderRect(kElBand0 + i).topLeft(), m_settings.bandsDb[i], m_pressed == kElBand0 + i);
}

// ------------------------------------------------------------------ input

int EqualizerWindow::hitTest(QPoint p) const {
    if (contains({E::kClose, QSize(9, 9)}, p)) return kElClose;
    if (contains({E::kOnPos, QSize(26, 12)}, p)) return kElOn;
    if (contains({E::kAutoPos, QSize(32, 12)}, p)) return kElAuto;
    if (contains({E::kPresetsPos, QSize(44, 12)}, p)) return kElPresets;
    if (contains(sliderRect(kElPreamp), p)) return kElPreamp;
    for (int i = 0; i < kEqBands; ++i)
        if (contains(sliderRect(kElBand0 + i), p)) return kElBand0 + i;
    return kElNone;
}

bool EqualizerWindow::isDragArea(QPoint p) const {
    return hitTest(p) == kElNone;
}

double* EqualizerWindow::valueFor(int el) {
    if (el == kElPreamp) return &m_settings.preampDb;
    if (el >= kElBand0 && el < kElBand0 + kEqBands) return &m_settings.bandsDb[el - kElBand0];
    return nullptr;
}

void EqualizerWindow::changed(int el) {
    Q_EMIT settingsChanged(m_settings);
    if (const double* v = valueFor(el)) {
        const QString name = el == kElPreamp ? QStringLiteral("PREAMP") : bandName(el - kElBand0);
        Q_EMIT statusText(QStringLiteral("EQ: %1 %2%3 DB").arg(name, *v >= 0 ? QStringLiteral("+") : QString()).arg(*v, 0, 'f', 1));
    }
    update();
}

void EqualizerWindow::setFromMouse(int el, QPoint p) {
    double* v = valueFor(el);
    if (!v) return;
    const QRect r = sliderRect(el);
    const double top = std::clamp(double(p.y() - r.y()) - 5.5, 0.0, double(E::kSliderTravel));
    double db = kEqMaxDb - top / E::kSliderTravel * 2 * kEqMaxDb;
    db = std::round(db * 10) / 10;
    if (std::abs(db) < 0.6) db = 0;  // centre detent
    if (db == *v) return;
    *v = db;
    changed(el);
}

bool EqualizerWindow::skinMousePress(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton) return false;
    const int el = hitTest(pos);
    if (el == kElNone) return false;
    m_pressed = el;
    m_pressedInside = true;
    setFromMouse(el, pos);
    update();
    return true;
}

void EqualizerWindow::skinMouseMove(QPoint pos) {
    if (m_pressed == kElNone) return;
    if (valueFor(m_pressed)) setFromMouse(m_pressed, pos);
    else m_pressedInside = hitTest(pos) == m_pressed;
    update();
}

void EqualizerWindow::skinMouseRelease(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton || m_pressed == kElNone) return;
    const int el = m_pressed;
    const bool inside = hitTest(pos) == el;
    m_pressed = kElNone;
    update();
    if (!inside) return;
    switch (el) {
    case kElClose: Q_EMIT closeRequested(); break;
    case kElOn:
        m_settings.enabled = !m_settings.enabled;
        Q_EMIT settingsChanged(m_settings);
        Q_EMIT statusText(m_settings.enabled ? QStringLiteral("EQ: ON") : QStringLiteral("EQ: OFF"));
        break;
    case kElAuto: m_auto = !m_auto; break;
    case kElPresets: showPresets(); break;
    default: break;
    }
}

bool EqualizerWindow::skinMouseDoubleClick(QPoint pos, Qt::MouseButton button) {
    // Double click on a slider resets it to 0 dB.
    const int el = hitTest(pos);
    double* v = valueFor(el);
    if (button != Qt::LeftButton || !v) return false;
    *v = 0;
    changed(el);
    return true;
}

void EqualizerWindow::wheelEvent(QWheelEvent* e) {
    const int el = hitTest(toSkin(e->position()));
    double* v = valueFor(el);
    const int steps = wheelSteps(e);
    if (!v || steps == 0) return;
    *v = std::clamp(std::round((*v + steps * 0.5) * 10) / 10, -kEqMaxDb, kEqMaxDb);
    changed(el);
}

void EqualizerWindow::showPresets() {
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction(QStringLiteral("Сбросить (0 дБ)"), this, [this] {
        const bool enabled = m_settings.enabled;
        m_settings = EqSettings{};
        m_settings.enabled = enabled;
        Q_EMIT settingsChanged(m_settings);
        update();
    });
    menu->addSeparator();
    for (const audio::EqPreset& preset : audio::builtinEqPresets()) {
        menu->addAction(preset.name, this, [this, preset] {
            const bool enabled = m_settings.enabled;
            m_settings = preset.settings;
            m_settings.enabled = enabled;
            Q_EMIT settingsChanged(m_settings);
            Q_EMIT statusText(QStringLiteral("EQ: ") + preset.name.toUpper());
            update();
        });
    }
    const QPoint at(E::kPresetsPos.x(), E::kPresetsPos.y() + 12);
    menu->popup(mapToGlobal(QPoint(qRound(at.x() * scale()), qRound(at.y() * scale()))));
}

void EqualizerWindow::closeEvent(QCloseEvent* e) {
    // Window manager close: just hide (the owner keeps the EQ/PL buttons in sync).
    e->ignore();
    Q_EMIT closeRequested();
}

}  // namespace qiyaa
