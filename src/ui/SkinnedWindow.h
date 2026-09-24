// Base class for every Winamp window: a frameless top-level OS window painted
// from skin sprites, shaped by region.txt, moved with snapping and always kept
// inside the visible screen area.
#pragma once

#include <cmath>

#include <QImage>
#include <QList>
#include <QPoint>
#include <QSize>
#include <QWidget>

namespace qiyaa {

class Skin;

class SkinnedWindow : public QWidget {
    Q_OBJECT
public:
    SkinnedWindow(const Skin* skin, QSize baseSize, QWidget* parent = nullptr);
    ~SkinnedWindow() override;

    void setSkin(const Skin* skin);
    const Skin& skin() const { return *m_skin; }

    // Zoom factor: 1 = original 275px wide, 2 = "double size", 1.5 = in between.
    // Clamped to [1, 4] and rounded to 0.05.
    void setScale(double scale);
    double scale() const { return m_scale; }
    static bool isIntegerScale(double s) { return std::abs(s - std::round(s)) < 1e-6; }

    // Places the window at `pos` (e.g. restored from settings), clamped to the
    // visible area of the screens that exist right now.
    void placeAt(QPoint pos);
    void ensureVisible();

    // true when the platform lets us position windows (X11, Windows, macOS, XWayland).
    static bool canPositionWindows();

    // All live skinned windows (used for snapping them to each other).
    static const QList<SkinnedWindow*>& allWindows();

Q_SIGNALS:
    void moveFinished(QPoint pos);

protected:
    // Paint in skin coordinates; the painter is already scaled.
    virtual void paintSkin(QPainter& p) = 0;
    // Where a press starts dragging the window (title bar etc.), in skin coordinates.
    virtual bool isDragArea(QPoint skinPos) const = 0;
    // Mouse handling for controls, in skin coordinates. Return true if consumed.
    virtual bool skinMousePress(QPoint, Qt::MouseButton) { return false; }
    virtual void skinMouseMove(QPoint) {}
    virtual void skinMouseRelease(QPoint, Qt::MouseButton) {}
    virtual void skinChanged() {}
    // Section of region.txt to use as the window mask ("normal", "windowshade", ...).
    virtual QString regionSection() const { return QStringLiteral("normal"); }

    QPoint toSkin(QPointF widgetPos) const;
    void applyMask();

    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void changeEvent(QEvent*) override;

private:
    const Skin* m_skin;
    QSize m_baseSize;
    double m_scale = 1.0;
    QImage m_buffer;  // intermediate image for fractional scales
    bool m_dragging = false;
    QPoint m_dragOffset;  // cursor - window top-left, in global coordinates
};

}  // namespace qiyaa
