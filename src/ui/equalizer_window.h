// The Winamp equalizer window (275x116): ON/AUTO, presets, preamp + 10 bands.
#pragma once

#include "audio/eq_presets.h"
#include "audio/equalizer.h"
#include "ui/skinned_window.h"

namespace Ui {

class EqualizerWindow : public SkinnedWindow {
    Q_OBJECT
public:
    explicit EqualizerWindow(const Skins::Skin* skin, QWidget* parent = nullptr);

    const Audio::EqSettings& settings() const { return equalizerSettings; }
    void setSettings(const Audio::EqSettings& s);
    bool autoOn() const { return autoEnabled; }
    void setShaded(bool shaded) override;
    // Volume/balance shown in shade mode (they belong to the main window).
    void setMixer(int volume, int balance);
    void setAutoOn(bool on) {
        autoEnabled = on;
        update();
    }

    // Spline through the band values, as drawn in the little graph (for tests).
    static QList<double> GraphCurve(const Audio::EqSettings& s);

Q_SIGNALS:
    void settingsChanged(const Audio::EqSettings& settings);
    void statusText(const QString& text);  // e.g. "EQ: 60HZ +3.0 DB" for the main marquee
    void closeRequested();
    void volumeRequested(int volume);
    void balanceRequested(int balance);

protected:
    void closeEvent(QCloseEvent* e) override;
    void paintSkin(QPainter& p) override;
    bool isDragArea(QPoint pos) const override;
    bool skinMousePress(QPoint pos, Qt::MouseButton button) override;
    void skinMouseMove(QPoint pos) override;
    void skinMouseRelease(QPoint pos, Qt::MouseButton button) override;
    bool skinMouseDoubleClick(QPoint pos, Qt::MouseButton button) override;
    QString regionSection() const override {
        return isShaded() ? QStringLiteral("equalizerws") : QStringLiteral("equalizer");
    }
    void wheelEvent(QWheelEvent* e) override;

private:
    // Element ids: see EqualizerWindow.cpp (kElClose, ..., kElBand0 + band).
    int hitTest(QPoint p) const;
    double* valueFor(int element);
    void setFromMouse(int element, QPoint p);
    void changed(int element);
    void drawSlider(QPainter& p, QPoint at, double db, bool active) const;
    void drawGraph(QPainter& p) const;
    void showPresets();
    void applyPreset(const Audio::EqPreset& preset);
    void loadEqf();
    void saveEqf();
    void paintShaded(QPainter& p);

    Audio::EqSettings equalizerSettings;
    bool autoEnabled = false;
    int pressedElement = 0;  // kElNone
    bool pressedInside = false;
    int volumePercent = 75;
    int balancePercent = 0;
};

}  // namespace Ui
