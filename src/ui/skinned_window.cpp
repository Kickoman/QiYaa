#include "ui/skinned_window.h"

#include "skins/skin.h"
#include "ui/snap.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTransform>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace Ui {

namespace {
QList<SkinnedWindow*>& WindowRegistry() {
    static QList<SkinnedWindow*> list;
    return list;
}

QList<QRect> ScreenRects() {
    QList<QRect> out;
    for (QScreen* targetScreen : QGuiApplication::screens()) {
        out << targetScreen->availableGeometry();
    }
    return out;
}
}  // namespace

SkinnedWindow::SkinnedWindow(const Skins::Skin* skin, QSize skinSize, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint)
    , currentSkin(skin)
    , skinPixelSize(skinSize) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    applySize();
    WindowRegistry().append(this);

    // Monitor unplugged / resolution changed: pull the window back on screen.
    auto recheck = [this] {
        if (isVisible()) {
            ensureVisible();
        }
    };
    connect(qApp, &QGuiApplication::screenRemoved, this, recheck);
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen* s) {
        connect(s, &QScreen::availableGeometryChanged, this, [this] {
            if (isVisible()) {
                ensureVisible();
            }
        });
    });
    for (QScreen* targetScreen : QGuiApplication::screens()) {
        connect(targetScreen, &QScreen::availableGeometryChanged, this, recheck);
    }
}

SkinnedWindow::~SkinnedWindow() {
    WindowRegistry().removeAll(this);
}

const QList<SkinnedWindow*>& SkinnedWindow::AllWindows() {
    return WindowRegistry();
}

bool SkinnedWindow::CanPositionWindows() {
    return !QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

void SkinnedWindow::setSecondary() {
#if defined(Q_OS_WIN)
    setWindowFlag(Qt::Tool, true);
#endif
}

void SkinnedWindow::setSkin(const Skins::Skin* skin) {
    currentSkin = skin;
    applyMask();
    skinChanged();
    update();
}

void SkinnedWindow::applySize() {
    setFixedSize(QSize(
        qRound(skinPixelSize.width() * scaleFactor), qRound(skinPixelSize.height() * scaleFactor)
    ));
    buffer = QImage();
}

void SkinnedWindow::setScale(double scale) {
    scale = std::round(std::clamp(scale, 1.0, 4.0) * 20.0) / 20.0;
    if (std::abs(scale - scaleFactor) < 1e-6) {
        return;
    }
    scaleFactor = scale;
    applySize();
    applyMask();
    ensureVisible();
    update();
}

void SkinnedWindow::setSkinSize(QSize size) {
    if (size == skinPixelSize) {
        return;
    }
    skinPixelSize = size;
    applySize();
    applyMask();
    update();
}

void SkinnedWindow::placeAt(QPoint pos) {
    if (!CanPositionWindows()) {
        return;
    }
    QRect rect(pos, size());
    move(Ui::ClampInside(rect, Ui::PickScreen(rect, ScreenRects())));
}

void SkinnedWindow::ensureVisible() {
    placeAt(pos());
}

void SkinnedWindow::applyShade(bool shaded, QSize newSkinSize) {
    shadeEnabled = shaded;
    resizeKeepingStack(newSkinSize);
    applyMask();  // the region section changes with the mode even if the size doesn't
    Q_EMIT shadeChanged(shaded);  // last: listeners save positions, which are final now
}

void SkinnedWindow::resizeKeepingStack(QSize newSkinSize) {
    // Hidden windows too, so they are still docked when shown again.
    QList<SkinnedWindow*> all;
    QList<QRect> rects;
    QList<bool> visible;
    int self = -1;
    for (SkinnedWindow* window : WindowRegistry()) {
        if (window == this) {
            self = int(all.size());
        }
        all << window;
        rects << window->frameGeometry();
        visible << window->isVisible();
    }
    const int oldHeight = height();
    setSkinSize(newSkinSize);
    const int dy = height() - oldHeight;
    if (dy == 0 || !CanPositionWindows()) {
        return;
    }
    QList<SkinnedWindow*> group{this};
    for (int i : Ui::StackBelow(self, rects, dy, visible)) {
        all[i]->move(all[i]->pos() + QPoint(0, dy));
        group << all[i];
    }
    // Growing near the bottom of the screen: lift the whole stack back onto it.
    if (dy < 0 || !isVisible()) {
        return;
    }
    for (SkinnedWindow* window : dockedWindows()) {
        if (!group.contains(window)) {
            group << window;
        }
    }
    QRect bounds;
    for (SkinnedWindow* window : group) {
        if (window->isVisible()) {
            bounds |= window->frameGeometry();
        }
    }
    const QPoint shift =
        Ui::ClampInside(bounds, Ui::PickScreen(bounds, ScreenRects())) - bounds.topLeft();
    if (!shift.isNull()) {
        for (SkinnedWindow* window : group) {
            window->move(window->pos() + shift);
        }
    }
}

QList<SkinnedWindow*> SkinnedWindow::dockedWindows() const {
    QList<SkinnedWindow*> visible;
    QList<QRect> rects;
    int self = -1;
    for (SkinnedWindow* window : WindowRegistry()) {
        if (!window->isVisible()) {
            continue;
        }
        if (window == this) {
            self = int(visible.size());
        }
        visible << window;
        rects << window->frameGeometry();
    }
    QList<SkinnedWindow*> out;
    for (int i : Ui::ConnectedGroup(self, rects)) {
        out << visible[i];
    }
    return out;
}

QPoint SkinnedWindow::toSkin(QPointF widgetPos) const {
    return QPoint(
        int(std::floor(widgetPos.x() / scaleFactor)), int(std::floor(widgetPos.y() / scaleFactor))
    );
}

int SkinnedWindow::wheelSteps(QWheelEvent* event) {
    wheelAccum += event->angleDelta().y();
    const int steps = wheelAccum / 120;
    wheelAccum -= steps * 120;
    return steps;
}

void SkinnedWindow::updateSkinRect(const QRect& rect) {
    const QRectF scaled(
        rect.x() * scaleFactor, rect.y() * scaleFactor, rect.width() * scaleFactor,
        rect.height() * scaleFactor
    );
    update(scaled.toAlignedRect().adjusted(-1, -1, 1, 1));
}

void SkinnedWindow::applyMask() {
    const QString section = regionSection();
    const auto it =
        section.isEmpty() ? currentSkin->region().cend() : currentSkin->region().constFind(section);
    if (it == currentSkin->region().cend()) {
        clearMask();
        return;
    }
    // Scale the polygons themselves (not the region) so fractional scales stay accurate.
    const QTransform t = QTransform::fromScale(scaleFactor, scaleFactor);
    QList<QPolygon> scaled;
    for (const QPolygon& poly : *it) {
        scaled << t.map(QPolygonF(poly)).toPolygon();
    }
    setMask(Skins::RegionFromPolygons(scaled));
}

void SkinnedWindow::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    if (IsIntegerScale(scaleFactor)) {
        // Integer scale + no smoothing = crisp pixels.
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.scale(scaleFactor, scaleFactor);
        paintSkin(painter);
        return;
    }
    // Fractional scale: nearest-neighbour at 1.5x would make some skin pixels 1px
    // and others 2px wide. Instead draw crisply at the next integer scale that
    // covers the physical pixels, then scale that down smoothly ("sharp bilinear").
    const int n = int(std::ceil(scaleFactor * devicePixelRatioF() - 1e-6));
    const QSize bufSize = skinPixelSize * n;
    if (buffer.size() != bufSize) {
        buffer = QImage(bufSize, QImage::Format_ARGB32_Premultiplied);
    }
    {
        QPainter bp(&buffer);
        bp.setRenderHint(QPainter::SmoothPixmapTransform, false);
        bp.scale(n, n);
        paintSkin(bp);
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(rect(), buffer);
}

void SkinnedWindow::mousePressEvent(QMouseEvent* event) {
    const QPoint sp = toSkin(event->position());
    if (skinMousePress(sp, event->button())) {
        return;
    }
    if (event->button() != Qt::LeftButton || !isDragArea(sp)) {
        return;
    }

    if (!CanPositionWindows()) {
        // Native Wayland: let the compositor move us (no snapping possible).
        if (QWindow* window = windowHandle()) {
            window->startSystemMove();
        }
        return;
    }
    dragging = true;
    pressGlobal = event->globalPosition().toPoint();
    dragGroup.clear();
    dragGroup.append({this, pos()});
    if (dragsDocked) {
        for (SkinnedWindow* window : dockedWindows()) {
            dragGroup.append({window, window->pos()});
        }
    }
    groupStartBounds = QRect();
    for (const auto& [window, start] : dragGroup) {
        groupStartBounds |= QRect(start, window->size());
    }
}

void SkinnedWindow::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging) {
        skinMouseMove(toSkin(event->position()));
        return;
    }
    // The whole group moves as one rectangle: it snaps to windows outside the
    // group and to screen edges, and is clamped to the screen as a unit.
    const QPoint delta = event->globalPosition().toPoint() - pressGlobal;
    QList<QRect> others;
    for (SkinnedWindow* window : WindowRegistry()) {
        if (!window->isVisible()) {
            continue;
        }
        const bool inGroup =
            std::any_of(dragGroup.cbegin(), dragGroup.cend(), [window](const auto& g) {
                return g.first == window;
            });
        if (!inGroup) {
            others << window->frameGeometry();
        }
    }
    const QPoint target =
        Ui::ResolveDragPosition(groupStartBounds.translated(delta), others, ScreenRects());
    const QPoint applied = target - groupStartBounds.topLeft();
    for (const auto& [window, start] : dragGroup) {
        if (window && window->pos() != start + applied) {
            window->move(start + applied);
        }
    }
}

void SkinnedWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (dragging && event->button() == Qt::LeftButton) {
        dragging = false;
        dragGroup.clear();
        Q_EMIT moveFinished();
        return;
    }
    skinMouseRelease(toSkin(event->position()), event->button());
}

void SkinnedWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    if (!skinMouseDoubleClick(toSkin(event->position()), event->button())) {
        mousePressEvent(event);
    }
}

void SkinnedWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::ActivationChange) {
        update();
    }
    QWidget::changeEvent(event);
}

}  // namespace Ui
