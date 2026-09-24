// The Winamp main window (275x116).
#pragma once

#include <QElapsedTimer>
#include <QTimer>

#include "ui/SkinnedWindow.h"

class QMenu;

namespace qiyaa {

class Player;

class MainWindow : public SkinnedWindow {
    Q_OBJECT
public:
    MainWindow(Player* player, const Skin* skin, QWidget* parent = nullptr);

    void setVolume(int v);
    void setBalance(int b);
    int volume() const { return m_volume; }
    int balance() const { return m_balance; }

    void setStatusText(const QString& text);  // shown in the marquee for a few seconds

Q_SIGNALS:
    void skinRequested(const QString& path);  // ":/skins/x.wsz" or a file path
    void scaleRequested(double scale);
    void alwaysOnTopRequested(bool on);

protected:
    void paintSkin(QPainter& p) override;
    bool isDragArea(QPoint skinPos) const override;
    bool skinMousePress(QPoint pos, Qt::MouseButton button) override;
    void skinMouseMove(QPoint pos) override;
    void skinMouseRelease(QPoint pos, Qt::MouseButton button) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    enum class Element {
        None, Options, Minimize, Shade, Close,
        Previous, Play, Pause, Stop, Next, Eject,
        Shuffle, Repeat, EqToggle, PlToggle,
        Volume, Balance, Position, Marquee,
    };

    Element hitTest(QPoint p) const;
    void activate(Element e);
    void updateSliderFromMouse(Element e, QPoint p);
    void refreshTimer();
    void tick();
    QString marqueeText() const;
    QMenu* buildMenu();

    void drawButton(QPainter& p, Element e, const QPoint& at, const QRect& normal, const QRect& pressed) const;
    void drawTime(QPainter& p) const;

    Player* m_player;
    QTimer m_timer;
    QElapsedTimer m_blink;
    Element m_pressed = Element::None;
    bool m_pressedInside = false;
    int m_volume = 75;
    int m_balance = 0;
    double m_seekPreview = -1;  // 0..1 while dragging the position bar
    bool m_eqOn = false;
    bool m_plOn = false;

    QString m_status;
    QElapsedTimer m_statusAge;
    int m_marqueeOffset = 0;  // in characters
    QElapsedTimer m_marqueeStep;
};

}  // namespace qiyaa
