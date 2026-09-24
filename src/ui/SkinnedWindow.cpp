#include "ui/SkinnedWindow.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTransform>
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

SkinnedWindow::SkinnedWindow(const Skin* skin, QSize baseSize, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint), m_skin(skin), m_baseSize(baseSize) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setFixedSize(m_baseSize);
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

void SkinnedWindow::setSkin(const Skin* skin) {
    m_skin = skin;
    applyMask();
    skinChanged();
    update();
}

void SkinnedWindow::setScale(double scale) {
    scale = std::round(std::clamp(scale, 1.0, 4.0) * 20.0) / 20.0;
    if (std::abs(scale - m_scale) < 1e-6) return;
    m_scale = scale;
    m_buffer = QImage();
    setFixedSize(QSize(qRound(m_baseSize.width() * m_scale), qRound(m_baseSize.height() * m_scale)));
    applyMask();
    ensureVisible();
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

QPoint SkinnedWindow::toSkin(QPointF widgetPos) const {
    return QPoint(int(std::floor(widgetPos.x() / m_scale)), int(std::floor(widgetPos.y() / m_scale)));
}

void SkinnedWindow::applyMask() {
    const auto it = m_skin->region().constFind(regionSection());
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
    const QSize bufSize = m_baseSize * n;
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
    m_dragOffset = e->globalPosition().toPoint() - frameGeometry().topLeft();
}

void SkinnedWindow::mouseMoveEvent(QMouseEvent* e) {
    if (!m_dragging) {
        skinMouseMove(toSkin(e->position()));
        return;
    }
    const QRect proposed(e->globalPosition().toPoint() - m_dragOffset, size());
    QList<QRect> others;
    for (SkinnedWindow* w : registry())
        if (w != this && w->isVisible()) others << w->frameGeometry();
    const QPoint target = snap::resolveDragPosition(proposed, others, screenRects());
    if (target != pos()) move(target);
}

void SkinnedWindow::mouseReleaseEvent(QMouseEvent* e) {
    if (m_dragging && e->button() == Qt::LeftButton) {
        m_dragging = false;
        Q_EMIT moveFinished(pos());
        return;
    }
    skinMouseRelease(toSkin(e->position()), e->button());
}

void SkinnedWindow::changeEvent(QEvent* e) {
    if (e->type() == QEvent::ActivationChange) update();
    QWidget::changeEvent(e);
}

}  // namespace qiyaa
