#pragma once

#include "ui/skinned_window.h"

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

class QPainter;

namespace Ui {

class GenWindow : public SkinnedWindow {
    Q_OBJECT
public:
    GenWindow(const Skins::Skin* skin, const QString& title, QWidget* parent = nullptr);

    QSize sizeSteps() const { return resizeSteps; }
    void setSizeSteps(QSize steps);

Q_SIGNALS:
    void closeRequested();
    void sizeStepsChanged(QSize steps);

protected:
    QRect contentRect() const;
    virtual void paintContent(QPainter& painter, const QRect& area) = 0;
    virtual bool contentMousePress(QPoint, Qt::MouseButton) { return false; }

    void paintSkin(QPainter& painter) override;
    bool isDragArea(QPoint pos) const override;
    bool skinMousePress(QPoint pos, Qt::MouseButton button) override;
    void skinMouseMove(QPoint pos) override;
    void skinMouseRelease(QPoint pos, Qt::MouseButton button) override;
    void closeEvent(QCloseEvent* event) override;

private:
    enum class Drag { None, Close, Resize };
    void paintFrame(QPainter& painter);

    QString titleText;
    QSize resizeSteps{0, 0};
    Drag activeDrag = Drag::None;
    QPoint dragStart;
    QSize dragStartSteps;
};

}  // namespace Ui
