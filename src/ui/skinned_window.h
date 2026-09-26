#pragma once

#include <QImage>
#include <QList>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

#include <cmath>
#include <utility>

class QPainter;

namespace Skins {
class Skin;
}  // namespace Skins

namespace Ui {

class SkinnedWindow : public QWidget {
    Q_OBJECT
public:
    SkinnedWindow(const Skins::Skin* skin, QSize skinSize, QWidget* parent = nullptr);
    ~SkinnedWindow() override;

    void setSkin(const Skins::Skin* skin);
    const Skins::Skin& skin() const { return *currentSkin; }

    void setScale(double scale);
    double scale() const { return scaleFactor; }
    static bool IsIntegerScale(double s) { return std::abs(s - std::round(s)) < 1e-6; }

    QSize skinSize() const { return skinPixelSize; }
    void setSkinSize(QSize size);

    void setDragsDockedWindows(bool on) { dragsDocked = on; }

    void setSecondary();

    void placeAt(QPoint pos);
    void ensureVisible();

    QList<SkinnedWindow*> dockedWindows() const;

    bool isShaded() const { return shadeEnabled; }
    virtual void setShaded(bool shaded) { Q_UNUSED(shaded); }

    static bool CanPositionWindows();

    static const QList<SkinnedWindow*>& AllWindows();

Q_SIGNALS:
    void moveFinished();
    void shadeChanged(bool shaded);

protected:
    virtual void paintSkin(QPainter& painter) = 0;
    virtual bool isDragArea(QPoint skinPos) const = 0;
    virtual bool skinMousePress(QPoint, Qt::MouseButton) { return false; }
    virtual void skinMouseMove(QPoint) { }
    virtual void skinMouseRelease(QPoint, Qt::MouseButton) { }
    virtual bool skinMouseDoubleClick(QPoint, Qt::MouseButton) { return false; }
    virtual void skinChanged() { }
    virtual QString regionSection() const { return {}; }

    void resizeKeepingStack(QSize skinSize);
    void applyShade(bool shaded, QSize skinSize);

    QPoint toSkin(QPointF widgetPos) const;
    int wheelSteps(QWheelEvent* event);
    void applyMask();
    void updateSkinRect(const QRect& skinRect);

    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void changeEvent(QEvent*) override;

private:
    void applySize();

    const Skins::Skin* currentSkin;
    QSize skinPixelSize;
    double scaleFactor = 1.0;
    bool dragsDocked = false;
    bool shadeEnabled = false;
    QImage buffer;
    int wheelAccum = 0;

    bool dragging = false;
    QPoint pressGlobal;
    QRect groupStartBounds;
    QList<std::pair<QPointer<SkinnedWindow>, QPoint>> dragGroup;
};

}  // namespace Ui
