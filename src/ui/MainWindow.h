// The Winamp main window (275x116).
#pragma once

#include <memory>
#include <vector>

#include <QElapsedTimer>
#include <QTimer>

#include "ui/SkinnedWindow.h"
#include "vis/Visualizer.h"

namespace qiyaa {

class Player;

class MainWindow : public SkinnedWindow {
    Q_OBJECT
public:
    enum class VisMode { Spectrum, Oscilloscope, Off };

    MainWindow(Player* player, const Skin* skin, QWidget* parent = nullptr);
    ~MainWindow() override;

    void setVolume(int v);
    void setBalance(int b);
    int volume() const { return m_volume; }
    int balance() const { return m_balance; }

    // Reflects whether the equalizer / playlist windows are shown.
    void setEqButton(bool on);
    void setPlButton(bool on);

    VisMode visMode() const { return m_visMode; }
    void setVisMode(VisMode mode);
    bool showsRemainingTime() const { return m_remaining; }
    void setShowsRemainingTime(bool on);

    void setStatusText(const QString& text);  // shown in the marquee for a few seconds

Q_SIGNALS:
    void eqToggleRequested();
    void plToggleRequested();
    void menuRequested(QPoint globalPos);         // options button / right click
    void sourcesMenuRequested(QPoint globalPos);  // eject button
    void closeRequested();
    void minimizedChanged(bool minimized);

protected:
    void closeEvent(QCloseEvent* e) override;
    void paintSkin(QPainter& p) override;
    bool isDragArea(QPoint skinPos) const override;
    bool skinMousePress(QPoint pos, Qt::MouseButton button) override;
    void skinMouseMove(QPoint pos) override;
    void skinMouseRelease(QPoint pos, Qt::MouseButton button) override;
    QString regionSection() const override { return QStringLiteral("normal"); }
    void contextMenuEvent(QContextMenuEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void changeEvent(QEvent* e) override;

private:
    enum class Element {
        None, Options, Minimize, Shade, Close,
        Previous, Play, Pause, Stop, Next, Eject,
        Shuffle, Repeat, EqToggle, PlToggle,
        Volume, Balance, Position, Marquee, Visualizer, Time,
    };

    Element hitTest(QPoint p) const;
    void activate(Element e);
    void updateSliderFromMouse(Element e, QPoint p);
    void refreshTimer();
    void tick();
    void updateVis();
    QString marqueeText() const;
    QPoint globalAt(QPoint skinPos) const;

    void drawButton(QPainter& p, Element e, const QPoint& at, const QRect& normal, const QRect& pressed) const;
    void drawTime(QPainter& p) const;

    Player* m_player;
    QTimer m_timer;
    QElapsedTimer m_blink;
    QElapsedTimer m_lastFullRepaint;
    Element m_pressed = Element::None;
    bool m_pressedInside = false;
    int m_volume = 75;
    int m_balance = 0;
    double m_seekPreview = -1;  // 0..1 while dragging the position bar
    bool m_eqOn = false;
    bool m_plOn = false;
    bool m_remaining = false;

    QString m_status;
    QElapsedTimer m_statusAge;
    int m_marqueeOffset = 0;  // in characters
    QElapsedTimer m_marqueeStep;

    VisMode m_visMode = VisMode::Spectrum;
    std::unique_ptr<vis::Visualizer> m_vis;
    vis::Analyzer m_analyzer;
    std::vector<float> m_visL, m_visR, m_visMono;
    bool m_visActive = false;
};

}  // namespace qiyaa
