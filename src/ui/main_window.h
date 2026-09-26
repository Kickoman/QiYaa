// The Winamp main window (275x116).
#pragma once

#include "ui/skinned_window.h"
#include "vis/visualizer.h"

#include <QElapsedTimer>
#include <QTimer>

#include <memory>
#include <vector>

namespace Core {
class Player;
}  // namespace Core

namespace Ui {

class MainWindow : public SkinnedWindow {
    Q_OBJECT
public:
    enum class VisMode { Spectrum, Oscilloscope, Off };

    MainWindow(Core::Player* player, const Skins::Skin* skin, QWidget* parent = nullptr);
    ~MainWindow() override;

    void setVolume(int value);
    void setBalance(int value);
    int volume() const { return volumePercent; }
    int balance() const { return balancePercent; }

    // Reflects whether the equalizer / playlist windows are shown.
    void setEqButton(bool on);
    void setPlButton(bool on);

    VisMode visMode() const { return visualizationMode; }
    void setVisMode(VisMode mode);
    bool showsRemainingTime() const { return remainingTimeShown; }
    void setShowsRemainingTime(bool on);

    void setStatusText(const QString& text);  // shown in the marquee for a few seconds

    void setShaded(bool shaded) override;

Q_SIGNALS:
    void eqToggleRequested();
    void plToggleRequested();
    void menuRequested(QPoint globalPos);  // options button / right click
    void sourcesMenuRequested(QPoint globalPos);  // eject button
    void closeRequested();
    void minimizedChanged(bool minimized);
    void volumeChanged(int volume);
    void balanceChanged(int balance);

protected:
    void closeEvent(QCloseEvent* event) override;
    void paintSkin(QPainter& painter) override;
    bool isDragArea(QPoint skinPos) const override;
    bool skinMousePress(QPoint pos, Qt::MouseButton button) override;
    void skinMouseMove(QPoint pos) override;
    void skinMouseRelease(QPoint pos, Qt::MouseButton button) override;
    QString regionSection() const override {
        return isShaded() ? QStringLiteral("windowshade") : QStringLiteral("normal");
    }
    bool skinMouseDoubleClick(QPoint pos, Qt::MouseButton button) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    enum class Element {
        None,
        Options,
        Minimize,
        Shade,
        Close,
        Previous,
        Play,
        Pause,
        Stop,
        Next,
        Eject,
        Shuffle,
        Repeat,
        EqToggle,
        PlToggle,
        Volume,
        Balance,
        Position,
        Marquee,
        Visualizer,
        Time,
    };

    Element hitTest(QPoint point) const;
    void activate(Element element);
    void updateSliderFromMouse(Element element, QPoint point);
    void refreshTimer();
    void tick();
    void updateVis();
    QString marqueeText() const;
    QPoint globalAt(QPoint skinPos) const;

    void drawButton(
        QPainter& painter,
        Element element,
        const QPoint& at,
        const QRect& normal,
        const QRect& pressed
    ) const;
    void drawTime(QPainter& painter) const;
    void paintShaded(QPainter& painter);
    Element hitTestShaded(QPoint point) const;
    QString miniTimeText() const;

    Core::Player* corePlayer;
    QTimer timer;
    QElapsedTimer blink;
    QElapsedTimer lastFullRepaint;
    Element pressedElement = Element::None;
    bool pressedInside = false;
    int volumePercent = 75;
    int balancePercent = 0;
    double seekPreview = -1;  // 0..1 while dragging the position bar
    bool eqOn = false;
    bool plOn = false;
    bool remainingTimeShown = false;

    QString statusText;
    QElapsedTimer statusAge;
    int marqueeOffset = 0;  // in characters
    QElapsedTimer marqueeStep;

    VisMode visualizationMode = VisMode::Spectrum;
    std::unique_ptr<Vis::Visualizer> visualizer;
    Vis::Analyzer analyzer;
    std::vector<float> visL, visR, visMono;
    bool visActive = false;
};

}  // namespace Ui
