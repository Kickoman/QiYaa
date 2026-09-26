#include "ui/gen_window.h"

#include "skins/skin.h"
#include "skins/sprites.h"

#include <QCloseEvent>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace Ui {

using TSheet = Skins::Skin::Sheet;
using Skins::GenWindowSprites;

namespace {
constexpr QSize kMinSize{275, 116};
constexpr int kStepWidth = 25, kStepHeight = 29;
constexpr int kTopHeight = 20, kBottomHeight = 14, kLeftWidth = 11, kRightWidth = 8;

bool RectContains(const QRect& rect, QPoint point) {
    return point.x() >= rect.x() && point.y() >= rect.y() && point.x() < rect.x() + rect.width()
        && point.y() < rect.y() + rect.height();
}
}  // namespace

GenWindow::GenWindow(const Skins::Skin* skin, const QString& title, QWidget* parent)
    : SkinnedWindow(skin, kMinSize, parent)
    , titleText(title) { }

void GenWindow::setSizeSteps(QSize steps) {
    steps = steps.expandedTo(QSize(0, 0)).boundedTo(QSize(40, 40));
    if (steps == resizeSteps) {
        return;
    }
    resizeSteps = steps;
    setSkinSize(QSize(
        kMinSize.width() + steps.width() * kStepWidth,
        kMinSize.height() + steps.height() * kStepHeight
    ));
    Q_EMIT sizeStepsChanged(steps);
}

QRect GenWindow::contentRect() const {
    const QSize size = skinSize();
    return {
        kLeftWidth, kTopHeight, size.width() - kLeftWidth - kRightWidth,
        size.height() - kTopHeight - kBottomHeight
    };
}

void GenWindow::paintFrame(QPainter& painter) {
    const Skins::Skin& activeSkin = skin();
    const int window = skinSize().width(), h = skinSize().height();
    const bool active = isActiveWindow();

    const QRect centerFill = active ? Skins::GenWindowSprites::kTopCenterFillSelected
                                    : Skins::GenWindowSprites::kTopCenterFill;
    for (int x = 0; x < window; x += 25) {
        activeSkin.draw(painter, TSheet::Gen, centerFill, {x, 0});
    }
    const int titleW = activeSkin.genTextWidth(titleText) + 7;  // 4 px left + 3 px right padding
    const int fill = std::max(0, (window - 100 - titleW) / 2);
    const QRect lrFill = active ? Skins::GenWindowSprites::kTopLeftRightFillSelected
                                : Skins::GenWindowSprites::kTopLeftRightFill;
    for (int x = 25; x < 25 + fill; x += 25) {
        activeSkin.draw(
            painter, TSheet::Gen, lrFill.adjusted(0, 0, std::min(0, 25 + fill - x - 25), 0), {x, 0}
        );
    }
    const int rightFillX = 25 + fill + 25 + titleW + 25;
    for (int x = rightFillX; x < window - 25; x += 25) {
        activeSkin.draw(
            painter, TSheet::Gen, lrFill.adjusted(0, 0, std::min(0, window - 25 - x - 25), 0),
            {x, 0}
        );
    }
    activeSkin.draw(
        painter, TSheet::Gen,
        active ? Skins::GenWindowSprites::kTopLeftSelected : Skins::GenWindowSprites::kTopLeft,
        {0, 0}
    );
    activeSkin.draw(
        painter, TSheet::Gen,
        active ? Skins::GenWindowSprites::kTopLeftEndSelected
               : Skins::GenWindowSprites::kTopLeftEnd,
        {25 + fill, 0}
    );
    activeSkin.drawGenText(painter, {25 + fill + 25 + 4, 4}, titleText, active);
    activeSkin.draw(
        painter, TSheet::Gen,
        active ? Skins::GenWindowSprites::kTopRightEndSelected
               : Skins::GenWindowSprites::kTopRightEnd,
        {25 + fill + 25 + titleW, 0}
    );
    activeSkin.draw(
        painter, TSheet::Gen,
        active ? Skins::GenWindowSprites::kTopRightSelected : Skins::GenWindowSprites::kTopRight,
        {window - 25, 0}
    );
    if (activeDrag == Drag::Close) {
        activeSkin.draw(
            painter, TSheet::Gen, Skins::GenWindowSprites::kCloseSelected, {window - 11, 3}
        );
    }

    for (int y = kTopHeight; y < h - kBottomHeight; y += 29) {
        const int tile = std::min(29, h - kBottomHeight - y);
        activeSkin.draw(
            painter, TSheet::Gen, Skins::GenWindowSprites::kMiddleLeft.adjusted(0, 0, 0, tile - 29),
            {0, y}
        );
        activeSkin.draw(
            painter, TSheet::Gen,
            Skins::GenWindowSprites::kMiddleRight.adjusted(0, 0, 0, tile - 29),
            {window - kRightWidth, y}
        );
    }
    activeSkin.draw(
        painter, TSheet::Gen, Skins::GenWindowSprites::kMiddleLeftBottom,
        {0, h - kBottomHeight - 24}
    );
    activeSkin.draw(
        painter, TSheet::Gen, Skins::GenWindowSprites::kMiddleRightBottom,
        {window - kRightWidth, h - kBottomHeight - 24}
    );

    for (int x = 125; x < window - 125; x += 25) {
        activeSkin.draw(
            painter, TSheet::Gen, Skins::GenWindowSprites::kBottomFill, {x, h - kBottomHeight}
        );
    }
    activeSkin.draw(
        painter, TSheet::Gen, Skins::GenWindowSprites::kBottomLeft, {0, h - kBottomHeight}
    );
    activeSkin.draw(
        painter, TSheet::Gen, Skins::GenWindowSprites::kBottomRight,
        {window - 125, h - kBottomHeight}
    );
}

void GenWindow::paintSkin(QPainter& painter) {
    const QRect area = contentRect();
    painter.save();
    painter.setClipRect(area);
    paintContent(painter, area);
    painter.restore();
    paintFrame(painter);
}

bool GenWindow::isDragArea(QPoint pos) const {
    return pos.y() < kTopHeight;
}

bool GenWindow::skinMousePress(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton) {
        return false;
    }
    const int window = skinSize().width(), h = skinSize().height();
    dragStart = pos;
    if (RectContains(QRect(window - 11, 3, 9, 9), pos)) {
        activeDrag = Drag::Close;
        update();
        return true;
    }
    if (RectContains(QRect(window - 20, h - 20, 20, 20), pos)) {
        activeDrag = Drag::Resize;
        dragStartSteps = resizeSteps;
        return true;
    }
    if (RectContains(contentRect(), pos)) {
        return contentMousePress(pos, button);
    }
    return false;
}

void GenWindow::skinMouseMove(QPoint pos) {
    if (activeDrag != Drag::Resize) {
        return;
    }
    const QPoint d = pos - dragStart;
    setSizeSteps(QSize(
        dragStartSteps.width() + int(std::lround(double(d.x()) / kStepWidth)),
        dragStartSteps.height() + int(std::lround(double(d.y()) / kStepHeight))
    ));
}

void GenWindow::skinMouseRelease(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton) {
        return;
    }
    const Drag drag = activeDrag;
    activeDrag = Drag::None;
    if (drag == Drag::Close && RectContains(QRect(skinSize().width() - 11, 3, 9, 9), pos)) {
        Q_EMIT closeRequested();
    }
    update();
}

void GenWindow::closeEvent(QCloseEvent* event) {
    event->ignore();
    Q_EMIT closeRequested();
}

}  // namespace Ui
