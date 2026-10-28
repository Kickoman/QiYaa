#pragma once

#include <QFontMetrics>
#include <QPoint>
#include <QRect>
#include <QString>

class QKeyEvent;

namespace Ui {

class FormCanvas;

class LineEdit {
public:
    enum class KeyResult { Edited, Moved, Enter, Escape, Ignored };

    explicit LineEdit(int maxLength);

    const QString& text() const { return content; }
    void setText(const QString& text);
    int cursor() const { return caret; }
    bool hasSelection() const { return anchor != caret; }
    QString selectedText() const;

    KeyResult press(const QKeyEvent* event);
    static bool takes(const QKeyEvent* event);

    void paint(FormCanvas& canvas, const QRect& rect, bool focused, const QString& hint);
    void pressAt(const QRect& rect, QPoint pos, const QFontMetrics& metrics);

private:
    void insert(const QString& text);
    void erase(int from, int to);
    void moveTo(int position, bool select);
    bool removeSelection();
    int wordBefore(int position) const;
    int wordAfter(int position) const;

    int maxLength;
    QString content;
    int caret = 0;
    int anchor = 0;
    int scroll = 0;
};

}  // namespace Ui
