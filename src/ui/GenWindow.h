// Winamp's "generic" window frame from GEN.BMP (used by plugins: Media Library,
// Milkdrop...). Resizable in 25x29 steps; subclasses paint the content area.
#pragma once

#include "ui/SkinnedWindow.h"

namespace qiyaa {

class GenWindow : public SkinnedWindow {
    Q_OBJECT
public:
    GenWindow(const Skin* skin, const QString& title, QWidget* parent = nullptr);

    QSize sizeSteps() const { return m_steps; }
    void setSizeSteps(QSize steps);

Q_SIGNALS:
    void closeRequested();
    void sizeStepsChanged(QSize steps);

protected:
    // Inner area in skin coordinates (inside the frame).
    QRect contentRect() const;
    virtual void paintContent(QPainter& p, const QRect& area) = 0;
    virtual bool contentMousePress(QPoint, Qt::MouseButton) { return false; }

    void paintSkin(QPainter& p) override;
    bool isDragArea(QPoint pos) const override;
    bool skinMousePress(QPoint pos, Qt::MouseButton button) override;
    void skinMouseMove(QPoint pos) override;
    void skinMouseRelease(QPoint pos, Qt::MouseButton button) override;
    void closeEvent(QCloseEvent* e) override;

private:
    enum class Drag { None, Close, Resize };
    void paintFrame(QPainter& p);

    QString m_title;
    QSize m_steps{0, 0};
    Drag m_drag = Drag::None;
    QPoint m_dragStart;
    QSize m_dragStartSteps;
};

}  // namespace qiyaa
