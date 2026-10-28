#pragma once

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QString>

#include <functional>

class QPainter;

namespace Skins {
class Skin;
}  // namespace Skins

namespace Ui {

struct FormControl {
    QRect rect;
    QString label;
    bool enabled = true;
    std::function<void()> action;
    std::function<void(QPoint)> pressAt{};
};

class FormCanvas {
public:
    enum class Glyph { Play, Add };

    static constexpr int kPadding = 4;
    static constexpr int kGap = 3;

    FormCanvas(QPainter* target, const QRect& area, const QFont& font, const Skins::Skin& skin);

    int width() const { return right - left; }
    int textWidth(const QString& text) const { return metrics.horizontalAdvance(text); }
    int buttonHeight() const { return lineHeight + 3; }
    int buttonWidth(const QString& label) const { return textWidth(label) + 8; }

    void text(
        const QRect& rect,
        const QString& content,
        const QColor& color,
        bool bold = false,
        int flags = Qt::AlignLeft | Qt::AlignVCenter
    );
    void line(const QString& content, const QColor& color, bool bold = false);
    void paragraph(
        const QString& content,
        const QColor& color,
        int maxLines = 0,
        int flags = Qt::TextWordWrap,
        int from = -1,
        int to = -1
    );
    QRect button(int x, int top, const QString& label, bool enabled, std::function<void()> action, bool on = false);
    QRect glyphButton(int x, int top, Glyph glyph, const QString& label, bool enabled, std::function<void()> action);
    void light(int x, int centerY, bool on);

    QPainter* painter;
    QFont regular;
    QFont strong;
    QFontMetrics metrics;
    QColor normal, current, background, selected, dim;
    int lineHeight = 0;
    int left, right, bottom, y;
    QList<FormControl> controls;
};

}  // namespace Ui
