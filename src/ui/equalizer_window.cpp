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
using Sheet = Skins::Skin::Sheet;
using Skins::EqualizerSprites;

namespace {

constexpr int kElNone = 0, kElClose = 1, kElOn = 2, kElAuto = 3, kElPresets = 4, kElPreamp = 5,
              kElBand0 = 6;
constexpr int kElShade = 20, kElShadeVolume = 21, kElShadeBalance = 22;
constexpr QPoint kShadeButton{254, 3};
using Skins::EqualizerShadeSprites;

bool contains(const QRect& r, QPoint p) {
    return p.x() >= r.x() && p.y() >= r.y() && p.x() < r.x() + r.width()
        && p.y() < r.y() + r.height();
}

QRect sliderRect(int element) {
    const int x = element == kElPreamp ? Skins::EqualizerSprites::kPreampPos.x()
                                       : Skins::EqualizerSprites::kBandsX
            + (element - kElBand0) * Skins::EqualizerSprites::kBandStep;
    return {
        x, Skins::EqualizerSprites::kSlidersY, Skins::EqualizerSprites::kSliderSize.width(),
        Skins::EqualizerSprites::kSliderSize.height()
    };
}

QString bandName(int band) {
    const double hz = Audio::kEqBandHz[band];
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

double dbToGraphY(double db) {
    return (1.0 - (db + Audio::kEqMaxDb) / (2 * Audio::kEqMaxDb)) * (kGraphH - 1);
}

}  // namespace

EqualizerWindow::EqualizerWindow(const Skins::Skin* skin, QWidget* parent)
    : SkinnedWindow(skin, Skins::EqualizerSprites::kSize, parent) {
    setWindowTitle(QStringLiteral("QiYaa Equalizer"));
}

void EqualizerWindow::setSettings(const Audio::EqSettings& s) {
    m_settings = s;
    update();
}

QList<double> EqualizerWindow::graphCurve(const Audio::EqSettings& s) {
    QList<double> xs, ys;
    for (int i = 0; i < Audio::kEqBands; ++i) {
        xs << i * 12.0;
        ys << dbToGraphY(s.bandsDb[i]);
    }
    return naturalSpline(xs, ys);
}

// ------------------------------------------------------------------ painting

void EqualizerWindow::drawSlider(QPainter& p, QPoint at, double db, bool active) const {
    const int frame = int(std::lround((db + Audio::kEqMaxDb) / (2 * Audio::kEqMaxDb) * 27));
    const QRect src(
        Skins::EqualizerSprites::kSliderFrames.x() + (frame % 14) * 15,
        Skins::EqualizerSprites::kSliderFrames.y() + (frame / 14) * 65,
        Skins::EqualizerSprites::kSliderSize.width(), Skins::EqualizerSprites::kSliderSize.height()
    );
    skin().draw(p, Sheet::EqMain, src, at);
    const int thumbY = int(std::lround(
        (1.0 - (db + Audio::kEqMaxDb) / (2 * Audio::kEqMaxDb))
        * Skins::EqualizerSprites::kSliderTravel
    ));
    skin().draw(
        p, Sheet::EqMain,
        active ? Skins::EqualizerSprites::kThumbSelected : Skins::EqualizerSprites::kThumb,
        at + QPoint(1, thumbY)
    );
}

void EqualizerWindow::drawGraph(QPainter& p) const {
    const QPoint origin = Skins::EqualizerSprites::kGraphPos;
    skin().draw(p, Sheet::EqMain, Skins::EqualizerSprites::kGraphBackground, origin);
    skin().draw(
        p, Sheet::EqMain, Skins::EqualizerSprites::kPreampLine,
        origin + QPoint(0, int(std::lround(dbToGraphY(m_settings.preampDb))))
    );

    const QImage& sheet = skin().sheet(Sheet::EqMain);
    const QList<double> ys = graphCurve(m_settings);
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
            p.fillRect(origin.x() + 2 + x, origin.y() + yy, 1, 1, c);
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
    m_volume = volume;
    m_balance = balance;
    if (isShaded()) {
        update();
    }
}

void EqualizerWindow::paintShaded(QPainter& p) {
    const Skins::Skin& sk = skin();
    sk.draw(
        p, Sheet::EqEx,
        isActiveWindow() ? Skins::EqualizerShadeSprites::kShadeBackgroundSelected
                         : Skins::EqualizerShadeSprites::kShadeBackground,
        {0, 0}
    );
    // Thumb sprite changes with the value: left / centre / right third.
    const int vThird = std::clamp(m_volume * 3 / 101, 0, 2);
    const int vx = Skins::EqualizerShadeSprites::kVolume.x()
        + int(std::lround(m_volume / 100.0 * (Skins::EqualizerShadeSprites::kVolume.width() - 3)));
    sk.draw(
        p, Sheet::EqEx, Skins::EqualizerShadeSprites::kVolumeThumb[vThird],
        {vx, Skins::EqualizerShadeSprites::kVolume.y()}
    );
    const int bThird = std::clamp((m_balance + 100) * 3 / 201, 0, 2);
    const int bx = Skins::EqualizerShadeSprites::kBalance.x()
        + int(std::lround(
            (m_balance + 100) / 200.0 * (Skins::EqualizerShadeSprites::kBalance.width() - 3)
        ));
    sk.draw(
        p, Sheet::EqEx, Skins::EqualizerShadeSprites::kBalanceThumb[bThird],
        {bx, Skins::EqualizerShadeSprites::kBalance.y()}
    );
    if (m_pressed == kElShade && m_pressedInside) {
        sk.draw(p, Sheet::EqEx, Skins::EqualizerShadeSprites::kShadeButtonShadedDown, kShadeButton);
    }
    if (m_pressed == kElClose && m_pressedInside) {
        sk.draw(
            p, Sheet::EqEx, Skins::EqualizerShadeSprites::kCloseButtonDown,
            Skins::EqualizerSprites::kClose
        );
    }
}

void EqualizerWindow::paintSkin(QPainter& p) {
    if (isShaded()) {
        return paintShaded(p);
    }
    const Skins::Skin& sk = skin();
    sk.draw(p, Sheet::EqMain, Skins::EqualizerSprites::kBackground, {0, 0});
    sk.draw(
        p, Sheet::EqMain,
        isActiveWindow() ? Skins::EqualizerSprites::kTitleBarSelected
                         : Skins::EqualizerSprites::kTitleBar,
        {0, 0}
    );
    if (m_pressed == kElClose && m_pressedInside) {
        sk.draw(
            p, Sheet::EqMain, Skins::EqualizerSprites::kCloseButtonDown,
            Skins::EqualizerSprites::kClose
        );
    }
    if (m_pressed == kElShade && m_pressedInside) {
        sk.draw(p, Sheet::EqEx, Skins::EqualizerShadeSprites::kShadeButtonDown, kShadeButton);
    }

    auto toggle = [&](int el, const Skins::ToggleSprite& spr, bool on, QPoint at) {
        const bool down = m_pressed == el && m_pressedInside;
        sk.draw(
            p, Sheet::EqMain,
            on ? (down ? spr.onPressed : spr.on) : (down ? spr.offPressed : spr.off), at
        );
    };
    toggle(
        kElOn, Skins::EqualizerSprites::kOn, m_settings.enabled, Skins::EqualizerSprites::kOnPos
    );
    toggle(kElAuto, Skins::EqualizerSprites::kAuto, m_auto, Skins::EqualizerSprites::kAutoPos);
    sk.draw(
        p, Sheet::EqMain,
        m_pressed == kElPresets && m_pressedInside ? Skins::EqualizerSprites::kPresetsButtonSelected
                                                   : Skins::EqualizerSprites::kPresetsButton,
        Skins::EqualizerSprites::kPresetsPos
    );

    drawGraph(p);
    drawSlider(p, sliderRect(kElPreamp).topLeft(), m_settings.preampDb, m_pressed == kElPreamp);
    for (int i = 0; i < Audio::kEqBands; ++i) {
        drawSlider(
            p, sliderRect(kElBand0 + i).topLeft(), m_settings.bandsDb[i], m_pressed == kElBand0 + i
        );
    }
}

// ------------------------------------------------------------------ input

int EqualizerWindow::hitTest(QPoint p) const {
    if (contains({Skins::EqualizerSprites::kClose, QSize(9, 9)}, p)) {
        return kElClose;
    }
    if (contains({kShadeButton, QSize(9, 9)}, p)) {
        return kElShade;
    }
    if (isShaded()) {
        if (contains(Skins::EqualizerShadeSprites::kVolume, p)) {
            return kElShadeVolume;
        }
        if (contains(Skins::EqualizerShadeSprites::kBalance, p)) {
            return kElShadeBalance;
        }
        return kElNone;
    }
    if (contains({Skins::EqualizerSprites::kOnPos, QSize(26, 12)}, p)) {
        return kElOn;
    }
    if (contains({Skins::EqualizerSprites::kAutoPos, QSize(32, 12)}, p)) {
        return kElAuto;
    }
    if (contains({Skins::EqualizerSprites::kPresetsPos, QSize(44, 12)}, p)) {
        return kElPresets;
    }
    if (contains(sliderRect(kElPreamp), p)) {
        return kElPreamp;
    }
    for (int i = 0; i < Audio::kEqBands; ++i) {
        if (contains(sliderRect(kElBand0 + i), p)) {
            return kElBand0 + i;
        }
    }
    return kElNone;
}

bool EqualizerWindow::isDragArea(QPoint p) const {
    return hitTest(p) == kElNone;
}

double* EqualizerWindow::valueFor(int el) {
    if (el == kElPreamp) {
        return &m_settings.preampDb;
    }
    if (el >= kElBand0 && el < kElBand0 + Audio::kEqBands) {
        return &m_settings.bandsDb[el - kElBand0];
    }
    return nullptr;
}

void EqualizerWindow::changed(int el) {
    Q_EMIT settingsChanged(m_settings);
    if (const double* v = valueFor(el)) {
        const QString name = el == kElPreamp ? QStringLiteral("PREAMP") : bandName(el - kElBand0);
        Q_EMIT statusText(QStringLiteral("EQ: %1 %2%3 DB")
                              .arg(name, *v >= 0 ? QStringLiteral("+") : QString())
                              .arg(*v, 0, 'f', 1));
    }
    update();
}

void EqualizerWindow::setFromMouse(int el, QPoint p) {
    if (el == kElShadeVolume || el == kElShadeBalance) {
        const QRect r = el == kElShadeVolume ? Skins::EqualizerShadeSprites::kVolume
                                             : Skins::EqualizerShadeSprites::kBalance;
        const double frac = std::clamp((p.x() - r.x() - 1.5) / (r.width() - 3), 0.0, 1.0);
        if (el == kElShadeVolume) {
            Q_EMIT volumeRequested(int(std::lround(frac * 100)));
        } else {
            Q_EMIT balanceRequested(int(std::lround(frac * 200 - 100)));
        }
        return;
    }
    double* v = valueFor(el);
    if (!v) {
        return;
    }
    const QRect r = sliderRect(el);
    const double top = std::clamp(
        double(p.y() - r.y()) - 5.5, 0.0, double(Skins::EqualizerSprites::kSliderTravel)
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
    changed(el);
}

bool EqualizerWindow::skinMousePress(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton) {
        return false;
    }
    const int el = hitTest(pos);
    if (el == kElNone) {
        return false;
    }
    m_pressed = el;
    m_pressedInside = true;
    setFromMouse(el, pos);
    update();
    return true;
}

void EqualizerWindow::skinMouseMove(QPoint pos) {
    if (m_pressed == kElNone) {
        return;
    }
    if (valueFor(m_pressed) || m_pressed == kElShadeVolume || m_pressed == kElShadeBalance) {
        setFromMouse(m_pressed, pos);
    } else {
        m_pressedInside = hitTest(pos) == m_pressed;
    }
    update();
}

void EqualizerWindow::skinMouseRelease(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton || m_pressed == kElNone) {
        return;
    }
    const int el = m_pressed;
    const bool inside = hitTest(pos) == el;
    m_pressed = kElNone;
    update();
    if (!inside) {
        return;
    }
    switch (el) {
        case kElClose: Q_EMIT closeRequested(); break;
        case kElShade: setShaded(!isShaded()); break;
        case kElOn:
            m_settings.enabled = !m_settings.enabled;
            Q_EMIT settingsChanged(m_settings);
            Q_EMIT statusText(
                m_settings.enabled ? QStringLiteral("EQ: ON") : QStringLiteral("EQ: OFF")
            );
            break;
        case kElAuto: m_auto = !m_auto; break;
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

void EqualizerWindow::wheelEvent(QWheelEvent* e) {
    const int el = hitTest(toSkin(e->position()));
    double* v = valueFor(el);
    const int steps = wheelSteps(e);
    if (!v || steps == 0) {
        return;
    }
    *v = std::clamp(std::round((*v + steps * 0.5) * 10) / 10, -Audio::kEqMaxDb, Audio::kEqMaxDb);
    changed(el);
}

void EqualizerWindow::applyPreset(const Audio::EqPreset& preset) {
    const bool enabled = m_settings.enabled;
    m_settings = preset.settings;
    m_settings.enabled = enabled;
    Q_EMIT settingsChanged(m_settings);
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
    QFile f(path);
    QList<Audio::EqPreset> presets;
    if (!f.open(QIODevice::ReadOnly) || !Audio::parseEqf(f.readAll(), &presets)) {
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
    QFile f(path);
    if (f.open(QIODevice::WriteOnly) && f.write(Audio::writeEqf({{name, m_settings}})) > 0) {
        Q_EMIT statusText(QStringLiteral("EQ: сохранено"));
    } else {
        Q_EMIT statusText(QStringLiteral("EQ: не удалось сохранить"));
    }
}

void EqualizerWindow::showPresets() {
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction(QStringLiteral("Сбросить (0 дБ)"), this, [this] {
        const bool enabled = m_settings.enabled;
        m_settings = Audio::EqSettings{};
        m_settings.enabled = enabled;
        Q_EMIT settingsChanged(m_settings);
        update();
    });
    menu->addAction(QStringLiteral("Загрузить .eqf..."), this, &EqualizerWindow::loadEqf);
    menu->addAction(QStringLiteral("Сохранить в .eqf..."), this, &EqualizerWindow::saveEqf);
    menu->addSeparator();
    for (const Audio::EqPreset& preset : Audio::builtinEqPresets()) {
        menu->addAction(preset.name, this, [this, preset] { applyPreset(preset); });
    }
    const QPoint at(
        Skins::EqualizerSprites::kPresetsPos.x(), Skins::EqualizerSprites::kPresetsPos.y() + 12
    );
    menu->popup(mapToGlobal(QPoint(qRound(at.x() * scale()), qRound(at.y() * scale()))));
}

void EqualizerWindow::closeEvent(QCloseEvent* e) {
    // Window manager close: just hide (the owner keeps the EQ/PL buttons in sync).
    e->ignore();
    Q_EMIT closeRequested();
}

}  // namespace Ui
