#pragma once

#include "audio/eq_presets.h"
#include "audio/equalizer.h"
#include "ui/skinned_window.h"

#include <QList>
#include <QPoint>
#include <QString>
#include <QWidget>

class QPainter;

namespace Ui {

struct EqualizerControl {
    enum class Kind {
        None,
        Close,
        Shade,
        ShadeVolume,
        ShadeBalance,
        On,
        Auto,
        Presets,
        Preamp,
        Band
    };

    Kind kind = Kind::None;
    int band = 0;

    bool operator==(const EqualizerControl&) const = default;
};

class EqualizerWindow : public SkinnedWindow {
    Q_OBJECT
public:
    explicit EqualizerWindow(const Skins::Skin* skin, QWidget* parent = nullptr);

    const Audio::EqSettings& settings() const { return equalizerSettings; }
    void setSettings(const Audio::EqSettings& settings);
    bool autoOn() const { return autoEnabled; }
    void setShaded(bool shaded) override;
    void setMixer(int volume, int balance);
    void setAutoOn(bool on);

    static QList<double> GraphCurve(const Audio::EqSettings& settings);

Q_SIGNALS:
    void settingsChanged(const Audio::EqSettings& settings);
    void statusText(const QString& text);
    void closeRequested();
    void volumeRequested(int volume);
    void balanceRequested(int balance);

protected:
    void closeEvent(QCloseEvent* event) override;
    void paintSkin(QPainter& painter) override;
    bool isDragArea(QPoint skinPos) const override;
    bool skinMousePress(QPoint pos, Qt::MouseButton button) override;
    void skinMouseMove(QPoint pos) override;
    void skinMouseRelease(QPoint pos, Qt::MouseButton button) override;
    bool skinMouseDoubleClick(QPoint pos, Qt::MouseButton button) override;
    QString regionSection() const override;
    void wheelEvent(QWheelEvent* event) override;

private:
    EqualizerControl hitTest(QPoint point) const;
    double* valueFor(const EqualizerControl& control);
    void setFromMouse(const EqualizerControl& control, QPoint point);
    void changed(const EqualizerControl& control);
    void drawSlider(QPainter& painter, QPoint at, double db, bool active) const;
    void drawGraph(QPainter& painter) const;
    void showPresets();
    void applyPreset(const Audio::EqPreset& preset);
    void loadEqf();
    void saveEqf();
    void paintShaded(QPainter& painter);

    Audio::EqSettings equalizerSettings;
    bool autoEnabled = false;
    EqualizerControl pressedControl;
    bool pressedInside = false;
    int volumePercent = 75;
    int balancePercent = 0;
};

}  // namespace Ui
