#include "ui/form_canvas.h"

#include "skins/skin.h"

#include <QPainter>
#include <QPoint>
#include <QPolygon>

#include <algorithm>
#include <utility>

namespace Ui {

namespace {

QColor Mix(const QColor& a, const QColor& b) {
    return QColor((a.red() + b.red()) / 2, (a.green() + b.green()) / 2, (a.blue() + b.blue()) / 2);
}

}  // namespace

FormCanvas::FormCanvas(QPainter* target, const QRect& area, const QFont& font, const Skins::Skin& skin)
    : painter(target)
    , regular(font)
    , strong(font)
    , metrics(font)
    , left(area.x() + kPadding)
    , right(area.right() + 1 - kPadding)
    , bottom(area.bottom() + 1 - kPadding)
    , y(area.y() + kPadding) {
    strong.setBold(true);
    const Skins::Skin::PlaylistStyle& style = skin.playlistStyle();
    normal = style.normal;
    current = style.current;
    background = style.normalBackground;
    selected = style.selectedBackground;
    dim = Mix(style.normal, style.normalBackground);
    lineHeight = metrics.height() + 1;
}

void FormCanvas::text(const QRect& rect, const QString& content, const QColor& color, bool bold, int flags) {
    if (!painter) {
        return;
    }
    painter->setFont(bold ? strong : regular);
    painter->setPen(color);
    const QFontMetrics fontMetrics(bold ? strong : regular);
    painter->drawText(rect, flags, fontMetrics.elidedText(content, Qt::ElideRight, rect.width()));
}

void FormCanvas::line(const QString& content, const QColor& color, bool bold) {
    text(QRect(left, y, width(), lineHeight), content, color, bold);
    y += lineHeight;
}

void FormCanvas::paragraph(const QString& content, const QColor& color, int maxLines, int flags, int from, int to) {
    const int x = from < 0 ? left : from;
    const int w = (to < 0 ? right : to) - x;
    const QRect bounds = metrics.boundingRect(QRect(x, y, w, 10'000), Qt::AlignLeft | Qt::AlignTop | flags, content);
    int height = bounds.height();
    if (maxLines > 0) {
        height = std::min(height, maxLines * metrics.lineSpacing());
    }
    if (painter) {
        painter->save();
        painter->setClipRect(QRect(x, y, w, height), Qt::IntersectClip);
        painter->setFont(regular);
        painter->setPen(color);
        painter->drawText(QRect(x, y, w, 10'000), Qt::AlignLeft | Qt::AlignTop | flags, content);
        painter->restore();
    }
    y += height;
}

QRect FormCanvas::button(int x, int top, const QString& label, bool enabled, std::function<void()> action, bool on) {
    const QRect rect(x, top, buttonWidth(label), buttonHeight());
    if (painter) {
        if (on) {
            painter->fillRect(rect, selected);
        }
        painter->setPen(enabled ? normal : dim);
        painter->drawRect(rect.adjusted(0, 0, -1, -1));
        text(rect, label, enabled ? normal : dim, false, Qt::AlignCenter);
    }
    controls << FormControl{rect, label, enabled, std::move(action)};
    return rect;
}

QRect FormCanvas::
    glyphButton(int x, int top, Glyph glyph, const QString& label, bool enabled, std::function<void()> action) {
    const int side = buttonHeight();
    const QRect rect(x, top, side, side);
    if (painter) {
        const QColor color = enabled ? normal : dim;
        painter->setPen(color);
        painter->drawRect(rect.adjusted(0, 0, -1, -1));
        const QPoint center = rect.center();
        const int half = std::max(2, side / 4);
        if (glyph == Glyph::Play) {
            painter->setBrush(color);
            painter->drawPolygon(QPolygon{
                QPoint(center.x() - half + 1, center.y() - half),
                QPoint(center.x() + half, center.y()),
                QPoint(center.x() - half + 1, center.y() + half)
            });
            painter->setBrush(Qt::NoBrush);
        } else {
            painter->drawLine(center.x() - half, center.y(), center.x() + half, center.y());
            painter->drawLine(center.x(), center.y() - half, center.x(), center.y() + half);
        }
    }
    controls << FormControl{rect, label, enabled, std::move(action)};
    return rect;
}

void FormCanvas::light(int x, int centerY, bool on) {
    if (!painter) {
        return;
    }
    const QRect rect(x, centerY - 2, 4, 4);
    if (on) {
        painter->fillRect(rect, current);
    } else {
        painter->setPen(dim);
        painter->drawRect(rect.adjusted(0, 0, -1, -1));
    }
}

}  // namespace Ui
