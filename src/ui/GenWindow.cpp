#include "ui/GenWindow.h"

#include <algorithm>
#include <cmath>

#include <QCloseEvent>
#include <QPainter>

#include "skin/Skin.h"
#include "skin/SkinSprites.h"

namespace qiyaa {

using Sheet = Skin::Sheet;
namespace G = sprites::gen;

namespace {
constexpr QSize kMinSize{275, 116};
constexpr int kStepW = 25, kStepH = 29;
constexpr int kTopH = 20, kBottomH = 14, kLeftW = 11, kRightW = 8;

bool contains(const QRect& r, QPoint p) {
    return p.x() >= r.x() && p.y() >= r.y() && p.x() < r.x() + r.width() && p.y() < r.y() + r.height();
}
}  // namespace

GenWindow::GenWindow(const Skin* skin, const QString& title, QWidget* parent)
    : SkinnedWindow(skin, kMinSize, parent), m_title(title) {}

void GenWindow::setSizeSteps(QSize steps) {
    steps = steps.expandedTo(QSize(0, 0)).boundedTo(QSize(40, 40));
    if (steps == m_steps) return;
    m_steps = steps;
    setSkinSize(QSize(kMinSize.width() + steps.width() * kStepW, kMinSize.height() + steps.height() * kStepH));
    Q_EMIT sizeStepsChanged(steps);
}

QRect GenWindow::contentRect() const {
    const QSize s = skinSize();
    return {kLeftW, kTopH, s.width() - kLeftW - kRightW, s.height() - kTopH - kBottomH};
}

void GenWindow::paintFrame(QPainter& p) {
    const Skin& sk = skin();
    const int w = skinSize().width(), h = skinSize().height();
    const bool active = isActiveWindow();

    // Top: fill everything, then corners, ends and the title in the middle.
    const QRect centerFill = active ? G::kTopCenterFillSelected : G::kTopCenterFill;
    for (int x = 0; x < w; x += 25) sk.draw(p, Sheet::Gen, centerFill, {x, 0});
    const int titleW = sk.genTextWidth(m_title) + 7;  // 4 px left + 3 px right padding
    const int fill = std::max(0, (w - 100 - titleW) / 2);
    const QRect lrFill = active ? G::kTopLeftRightFillSelected : G::kTopLeftRightFill;
    for (int x = 25; x < 25 + fill; x += 25) sk.draw(p, Sheet::Gen, lrFill.adjusted(0, 0, std::min(0, 25 + fill - x - 25), 0), {x, 0});
    const int rightFillX = 25 + fill + 25 + titleW + 25;
    for (int x = rightFillX; x < w - 25; x += 25) sk.draw(p, Sheet::Gen, lrFill.adjusted(0, 0, std::min(0, w - 25 - x - 25), 0), {x, 0});
    sk.draw(p, Sheet::Gen, active ? G::kTopLeftSelected : G::kTopLeft, {0, 0});
    sk.draw(p, Sheet::Gen, active ? G::kTopLeftEndSelected : G::kTopLeftEnd, {25 + fill, 0});
    sk.drawGenText(p, {25 + fill + 25 + 4, 4}, m_title, active);
    sk.draw(p, Sheet::Gen, active ? G::kTopRightEndSelected : G::kTopRightEnd, {25 + fill + 25 + titleW, 0});
    sk.draw(p, Sheet::Gen, active ? G::kTopRightSelected : G::kTopRight, {w - 25, 0});
    if (m_drag == Drag::Close) sk.draw(p, Sheet::Gen, G::kCloseSelected, {w - 11, 3});

    // Sides: tiles, with the bottom pieces anchored to the bottom.
    for (int y = kTopH; y < h - kBottomH; y += 29) {
        const int tile = std::min(29, h - kBottomH - y);
        sk.draw(p, Sheet::Gen, G::kMiddleLeft.adjusted(0, 0, 0, tile - 29), {0, y});
        sk.draw(p, Sheet::Gen, G::kMiddleRight.adjusted(0, 0, 0, tile - 29), {w - kRightW, y});
    }
    sk.draw(p, Sheet::Gen, G::kMiddleLeftBottom, {0, h - kBottomH - 24});
    sk.draw(p, Sheet::Gen, G::kMiddleRightBottom, {w - kRightW, h - kBottomH - 24});

    // Bottom.
    for (int x = 125; x < w - 125; x += 25) sk.draw(p, Sheet::Gen, G::kBottomFill, {x, h - kBottomH});
    sk.draw(p, Sheet::Gen, G::kBottomLeft, {0, h - kBottomH});
    sk.draw(p, Sheet::Gen, G::kBottomRight, {w - 125, h - kBottomH});
}

void GenWindow::paintSkin(QPainter& p) {
    const QRect area = contentRect();
    p.save();
    p.setClipRect(area);
    paintContent(p, area);
    p.restore();
    paintFrame(p);
}

bool GenWindow::isDragArea(QPoint pos) const {
    return pos.y() < kTopH;
}

bool GenWindow::skinMousePress(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton) return false;
    const int w = skinSize().width(), h = skinSize().height();
    m_dragStart = pos;
    if (contains(QRect(w - 11, 3, 9, 9), pos)) {
        m_drag = Drag::Close;
        update();
        return true;
    }
    if (contains(QRect(w - 20, h - 20, 20, 20), pos)) {
        m_drag = Drag::Resize;
        m_dragStartSteps = m_steps;
        return true;
    }
    if (contains(contentRect(), pos)) return contentMousePress(pos, button);
    return false;
}

void GenWindow::skinMouseMove(QPoint pos) {
    if (m_drag != Drag::Resize) return;
    const QPoint d = pos - m_dragStart;
    setSizeSteps(QSize(m_dragStartSteps.width() + int(std::lround(double(d.x()) / kStepW)),
                       m_dragStartSteps.height() + int(std::lround(double(d.y()) / kStepH))));
}

void GenWindow::skinMouseRelease(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton) return;
    const Drag drag = m_drag;
    m_drag = Drag::None;
    if (drag == Drag::Close && contains(QRect(skinSize().width() - 11, 3, 9, 9), pos)) Q_EMIT closeRequested();
    update();
}

void GenWindow::closeEvent(QCloseEvent* e) {
    e->ignore();
    Q_EMIT closeRequested();
}

}  // namespace qiyaa
