// Base class for every Winamp window: a frameless top-level OS window painted
// from skin sprites, shaped by region.txt, moved with snapping and always kept
// inside the visible screen area.
#pragma once

#include <cmath>

#include <QImage>
#include <QList>
#include <QPoint>
#include <QPointer>
#include <QSize>
#include <QWidget>

namespace qiyaa {

class Skin;

class SkinnedWindow : public QWidget {
    Q_OBJECT
public:
    SkinnedWindow(const Skin* skin, QSize skinSize, QWidget* parent = nullptr);
    ~SkinnedWindow() override;

    void setSkin(const Skin* skin);
    const Skin& skin() const { return *m_skin; }

    // Zoom factor: 1 = original 275px wide, 2 = "double size", 1.5 = in between.
    // Clamped to [1, 4] and rounded to 0.05.
    void setScale(double scale);
    double scale() const { return m_scale; }
    static bool isIntegerScale(double s) { return std::abs(s - std::round(s)) < 1e-6; }

    // Size in skin pixels (before scaling). Resizable windows (playlist) change it.
    QSize skinSize() const { return m_skinSize; }
    void setSkinSize(QSize size);

    // When true, dragging this window also drags every window docked to it
    // (Winamp: the main window pulls the equalizer and playlist along).
    void setDragsDockedWindows(bool on) { m_dragsDocked = on; }

    // Secondary windows (EQ, playlist): no own taskbar button on Windows.
    void setSecondary();

    // Places the window at `pos` (e.g. restored from settings), clamped to the
    // visible area of the screens that exist right now.
    void placeAt(QPoint pos);
    void ensureVisible();

    // Visible windows that are docked (directly or through others) to this one.
    QList<SkinnedWindow*> dockedWindows() const;

    // true when the platform lets us position windows (X11, Windows, macOS, XWayland).
    static bool canPositionWindows();

    // All live skinned windows (used for snapping them to each other).
    static const QList<SkinnedWindow*>& allWindows();

Q_SIGNALS:
    void moveFinished();

protected:
    // Paint in skin coordinates; the painter is already scaled.
    virtual void paintSkin(QPainter& p) = 0;
    // Where a press starts dragging the window (title bar etc.), in skin coordinates.
    virtual bool isDragArea(QPoint skinPos) const = 0;
    // Mouse handling for controls, in skin coordinates. Return true if consumed.
    virtual bool skinMousePress(QPoint, Qt::MouseButton) { return false; }
    virtual void skinMouseMove(QPoint) {}
    virtual void skinMouseRelease(QPoint, Qt::MouseButton) {}
    // Return true if consumed; otherwise the double click acts as a normal press.
    virtual bool skinMouseDoubleClick(QPoint, Qt::MouseButton) { return false; }
    virtual void skinChanged() {}
    // Section of region.txt to use as the window mask ("normal", "equalizer", ...);
    // empty = rectangular window.
    virtual QString regionSection() const { return {}; }

    QPoint toSkin(QPointF widgetPos) const;
    // Whole wheel "notches" in this event, accumulating the small deltas that
    // touchpads and smooth-scrolling mice send. Positive = away from the user.
    int wheelSteps(QWheelEvent* e);
    void applyMask();
    // Repaint only a part of the window, given in skin coordinates.
    void updateSkinRect(const QRect& skinRect);

    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void changeEvent(QEvent*) override;

private:
    void applySize();

    const Skin* m_skin;
    QSize m_skinSize;
    double m_scale = 1.0;
    bool m_dragsDocked = false;
    QImage m_buffer;  // intermediate image for fractional scales
    int m_wheelAccum = 0;

    // Drag state.
    bool m_dragging = false;
    QPoint m_pressGlobal;
    QRect m_groupStartBounds;
    QList<std::pair<QPointer<SkinnedWindow>, QPoint>> m_group;  // window, start position
};

}  // namespace qiyaa
