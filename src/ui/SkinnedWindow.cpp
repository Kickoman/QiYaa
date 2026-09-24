#include "ui/SkinnedWindow.h"

#include <algorithm>
#include <cmath>

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTransform>
#include <QWheelEvent>
#include <QWindow>

#include "skin/Skin.h"
#include "ui/Snap.h"

namespace qiyaa {

namespace {
QList<SkinnedWindow*>& registry() {
    static QList<SkinnedWindow*> list;
    return list;
}

QList<QRect> screenRects() {
    QList<QRect> out;
    for (QScreen* s : QGuiApplication::screens()) out << s->availableGeometry();
    return out;
}
}  // namespace

SkinnedWindow::SkinnedWindow(const Skin* skin, QSize skinSize, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint), m_skin(skin), m_skinSize(skinSize) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    applySize();
    registry().append(this);

    // Monitor unplugged / resolution changed: pull the window back on screen.
    auto recheck = [this] { if (isVisible()) ensureVisible(); };
    connect(qApp, &QGuiApplication::screenRemoved, this, recheck);
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen* s) {
        connect(s, &QScreen::availableGeometryChanged, this, [this] { if (isVisible()) ensureVisible(); });
    });
    for (QScreen* s : QGuiApplication::screens())
        connect(s, &QScreen::availableGeometryChanged, this, recheck);
}

SkinnedWindow::~SkinnedWindow() { registry().removeAll(this); }

const QList<SkinnedWindow*>& SkinnedWindow::allWindows() { return registry(); }

bool SkinnedWindow::canPositionWindows() {
    return !QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

void SkinnedWindow::setSecondary() {
#if defined(Q_OS_WIN)
    setWindowFlag(Qt::Tool, true);
#endif
}

void SkinnedWindow::setSkin(const Skin* skin) {
    m_skin = skin;
    applyMask();
    skinChanged();
    update();
}

void SkinnedWindow::applySize() {
    setFixedSize(QSize(qRound(m_skinSize.width() * m_scale), qRound(m_skinSize.height() * m_scale)));
    m_buffer = QImage();
}

void SkinnedWindow::setScale(double scale) {
    scale = std::round(std::clamp(scale, 1.0, 4.0) * 20.0) / 20.0;
    if (std::abs(scale - m_scale) < 1e-6) return;
    m_scale = scale;
    applySize();
    applyMask();
    ensureVisible();
    update();
}

void SkinnedWindow::setSkinSize(QSize size) {
    if (size == m_skinSize) return;
    m_skinSize = size;
    applySize();
    applyMask();
    update();
}

void SkinnedWindow::placeAt(QPoint pos) {
    if (!canPositionWindows()) return;
    QRect r(pos, size());
    move(snap::clampInside(r, snap::pickScreen(r, screenRects())));
}

void SkinnedWindow::ensureVisible() {
    placeAt(pos());
}

void SkinnedWindow::applyShade(bool shaded, QSize newSkinSize) {
    m_shaded = shaded;
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
    for (SkinnedWindow* w : registry()) {
        if (w == this) self = int(all.size());
        all << w;
        rects << w->frameGeometry();
        visible << w->isVisible();
    }
    const int oldHeight = height();
    setSkinSize(newSkinSize);
    const int dy = height() - oldHeight;
    if (dy == 0 || !canPositionWindows()) return;
    QList<SkinnedWindow*> group{this};
    for (int i : snap::stackBelow(self, rects, dy, visible)) {
        all[i]->move(all[i]->pos() + QPoint(0, dy));
        group << all[i];
    }
    // Growing near the bottom of the screen: lift the whole stack back onto it.
    if (dy < 0 || !isVisible()) return;
    for (SkinnedWindow* w : dockedWindows())
        if (!group.contains(w)) group << w;
    QRect bounds;
    for (SkinnedWindow* w : group)
        if (w->isVisible()) bounds |= w->frameGeometry();
    const QPoint shift = snap::clampInside(bounds, snap::pickScreen(bounds, screenRects())) - bounds.topLeft();
    if (!shift.isNull())
        for (SkinnedWindow* w : group) w->move(w->pos() + shift);
}

QList<SkinnedWindow*> SkinnedWindow::dockedWindows() const {
    QList<SkinnedWindow*> visible;
    QList<QRect> rects;
    int self = -1;
    for (SkinnedWindow* w : registry()) {
        if (!w->isVisible()) continue;
        if (w == this) self = int(visible.size());
        visible << w;
        rects << w->frameGeometry();
    }
    QList<SkinnedWindow*> out;
    for (int i : snap::connectedGroup(self, rects)) out << visible[i];
    return out;
}

QPoint SkinnedWindow::toSkin(QPointF widgetPos) const {
    return QPoint(int(std::floor(widgetPos.x() / m_scale)), int(std::floor(widgetPos.y() / m_scale)));
}

int SkinnedWindow::wheelSteps(QWheelEvent* e) {
    m_wheelAccum += e->angleDelta().y();
    const int steps = m_wheelAccum / 120;
    m_wheelAccum -= steps * 120;
    return steps;
}

void SkinnedWindow::updateSkinRect(const QRect& r) {
    const QRectF scaled(r.x() * m_scale, r.y() * m_scale, r.width() * m_scale, r.height() * m_scale);
    update(scaled.toAlignedRect().adjusted(-1, -1, 1, 1));
}

void SkinnedWindow::applyMask() {
    const QString section = regionSection();
    const auto it = section.isEmpty() ? m_skin->region().cend() : m_skin->region().constFind(section);
    if (it == m_skin->region().cend()) {
        clearMask();
        return;
    }
    // Scale the polygons themselves (not the region) so fractional scales stay accurate.
    const QTransform t = QTransform::fromScale(m_scale, m_scale);
    QList<QPolygon> scaled;
    for (const QPolygon& poly : *it) scaled << t.map(QPolygonF(poly)).toPolygon();
    setMask(regionFromPolygons(scaled));
}

void SkinnedWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    if (isIntegerScale(m_scale)) {
        // Integer scale + no smoothing = crisp pixels.
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.scale(m_scale, m_scale);
        paintSkin(p);
        return;
    }
    // Fractional scale: nearest-neighbour at 1.5x would make some skin pixels 1px
    // and others 2px wide. Instead draw crisply at the next integer scale that
    // covers the physical pixels, then scale that down smoothly ("sharp bilinear").
    const int n = int(std::ceil(m_scale * devicePixelRatioF() - 1e-6));
    const QSize bufSize = m_skinSize * n;
    if (m_buffer.size() != bufSize) m_buffer = QImage(bufSize, QImage::Format_ARGB32_Premultiplied);
    {
        QPainter bp(&m_buffer);
        bp.setRenderHint(QPainter::SmoothPixmapTransform, false);
        bp.scale(n, n);
        paintSkin(bp);
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(rect(), m_buffer);
}

void SkinnedWindow::mousePressEvent(QMouseEvent* e) {
    const QPoint sp = toSkin(e->position());
    if (skinMousePress(sp, e->button())) return;
    if (e->button() != Qt::LeftButton || !isDragArea(sp)) return;

    if (!canPositionWindows()) {
        // Native Wayland: let the compositor move us (no snapping possible).
        if (QWindow* w = windowHandle()) w->startSystemMove();
        return;
    }
    m_dragging = true;
    m_pressGlobal = e->globalPosition().toPoint();
    m_group.clear();
    m_group.append({this, pos()});
    if (m_dragsDocked)
        for (SkinnedWindow* w : dockedWindows()) m_group.append({w, w->pos()});
    m_groupStartBounds = QRect();
    for (const auto& [w, start] : m_group) m_groupStartBounds |= QRect(start, w->size());
}

void SkinnedWindow::mouseMoveEvent(QMouseEvent* e) {
    if (!m_dragging) {
        skinMouseMove(toSkin(e->position()));
        return;
    }
    // The whole group moves as one rectangle: it snaps to windows outside the
    // group and to screen edges, and is clamped to the screen as a unit.
    const QPoint delta = e->globalPosition().toPoint() - m_pressGlobal;
    QList<QRect> others;
    for (SkinnedWindow* w : registry()) {
        if (!w->isVisible()) continue;
        const bool inGroup = std::any_of(m_group.cbegin(), m_group.cend(), [w](const auto& g) { return g.first == w; });
        if (!inGroup) others << w->frameGeometry();
    }
    const QPoint target = snap::resolveDragPosition(m_groupStartBounds.translated(delta), others, screenRects());
    const QPoint applied = target - m_groupStartBounds.topLeft();
    for (const auto& [w, start] : m_group)
        if (w && w->pos() != start + applied) w->move(start + applied);
}

void SkinnedWindow::mouseReleaseEvent(QMouseEvent* e) {
    if (m_dragging && e->button() == Qt::LeftButton) {
        m_dragging = false;
        m_group.clear();
        Q_EMIT moveFinished();
        return;
    }
    skinMouseRelease(toSkin(e->position()), e->button());
}

void SkinnedWindow::mouseDoubleClickEvent(QMouseEvent* e) {
    if (!skinMouseDoubleClick(toSkin(e->position()), e->button())) mousePressEvent(e);
}

void SkinnedWindow::changeEvent(QEvent* e) {
    if (e->type() == QEvent::ActivationChange) update();
    QWidget::changeEvent(e);
}

}  // namespace qiyaa
