// The Winamp equalizer window (275x116): ON/AUTO, presets, preamp + 10 bands.
#pragma once

#include "audio/EqPresets.h"
#include "audio/Equalizer.h"
#include "ui/SkinnedWindow.h"

namespace qiyaa {

class EqualizerWindow : public SkinnedWindow {
    Q_OBJECT
public:
    explicit EqualizerWindow(const Skin* skin, QWidget* parent = nullptr);

    const audio::EqSettings& settings() const { return m_settings; }
    void setSettings(const audio::EqSettings& s);
    bool autoOn() const { return m_auto; }
    void setShaded(bool shaded) override;
    // Volume/balance shown in shade mode (they belong to the main window).
    void setMixer(int volume, int balance);
    void setAutoOn(bool on) { m_auto = on; update(); }

    // Spline through the band values, as drawn in the little graph (for tests).
    static QList<double> graphCurve(const audio::EqSettings& s);

Q_SIGNALS:
    void settingsChanged(const audio::EqSettings& settings);
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
    QString regionSection() const override { return isShaded() ? QStringLiteral("equalizerws") : QStringLiteral("equalizer"); }
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
    void applyPreset(const audio::EqPreset& preset);
    void loadEqf();
    void saveEqf();
    void paintShaded(QPainter& p);

    audio::EqSettings m_settings;
    bool m_auto = false;
    int m_pressed = 0;  // kElNone
    bool m_pressedInside = false;
    int m_volume = 75;
    int m_balance = 0;
};

}  // namespace qiyaa
