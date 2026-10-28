#include "ui/line_edit.h"

#include "ui/form_canvas.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QPainter>

#include <algorithm>
#include <array>
#include <cstdlib>

namespace Ui {

namespace {

constexpr std::array kEditKeys{
    QKeySequence::Paste,
    QKeySequence::Copy,
    QKeySequence::Cut,
    QKeySequence::SelectAll,
    QKeySequence::Delete,
    QKeySequence::DeleteStartOfWord,
    QKeySequence::DeleteEndOfWord,
    QKeySequence::MoveToPreviousChar,
    QKeySequence::MoveToNextChar,
    QKeySequence::MoveToPreviousWord,
    QKeySequence::MoveToNextWord,
    QKeySequence::MoveToStartOfLine,
    QKeySequence::MoveToEndOfLine,
    QKeySequence::SelectPreviousChar,
    QKeySequence::SelectNextChar,
    QKeySequence::SelectPreviousWord,
    QKeySequence::SelectNextWord,
    QKeySequence::SelectStartOfLine,
    QKeySequence::SelectEndOfLine,
};

bool IsWordCharacter(QChar character) {
    return character.isLetterOrNumber();
}

}  // namespace

LineEdit::LineEdit(int maxLength)
    : maxLength(maxLength) { }

void LineEdit::setText(const QString& text) {
    content = text.left(maxLength);
    caret = static_cast<int>(content.size());
    anchor = caret;
}

QString LineEdit::selectedText() const {
    const int from = std::min(anchor, caret);
    return content.mid(from, std::abs(caret - anchor));
}

bool LineEdit::takes(const QKeyEvent* event) {
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
    return modifiers == Qt::NoModifier
        || std::any_of(kEditKeys.cbegin(), kEditKeys.cend(), [event](QKeySequence::StandardKey key) {
               return event->matches(key);
           });
}

LineEdit::KeyResult LineEdit::press(const QKeyEvent* event) {
    const int end = static_cast<int>(content.size());
    if (event->matches(QKeySequence::Paste)) {
        insert(QGuiApplication::clipboard()->text().simplified());
    } else if (event->matches(QKeySequence::Copy) || event->matches(QKeySequence::Cut)) {
        if (hasSelection()) {
            QGuiApplication::clipboard()->setText(selectedText());
        }
        if (event->matches(QKeySequence::Copy) || !removeSelection()) {
            return KeyResult::Moved;
        }
    } else if (event->matches(QKeySequence::SelectAll)) {
        anchor = 0;
        caret = end;
        return KeyResult::Moved;
    } else if (event->matches(QKeySequence::DeleteStartOfWord)) {
        if (!removeSelection()) {
            erase(wordBefore(caret), caret);
        }
    } else if (event->matches(QKeySequence::DeleteEndOfWord)) {
        if (!removeSelection()) {
            erase(caret, wordAfter(caret));
        }
    } else if (event->matches(QKeySequence::Delete)) {
        if (!removeSelection()) {
            erase(caret, std::min(caret + 1, end));
        }
    } else if (event->key() == Qt::Key_Backspace) {
        if (!removeSelection()) {
            erase(std::max(caret - 1, 0), caret);
        }
    } else if (event->matches(QKeySequence::MoveToPreviousWord) || event->matches(QKeySequence::SelectPreviousWord)) {
        moveTo(wordBefore(caret), event->matches(QKeySequence::SelectPreviousWord));
        return KeyResult::Moved;
    } else if (event->matches(QKeySequence::MoveToNextWord) || event->matches(QKeySequence::SelectNextWord)) {
        moveTo(wordAfter(caret), event->matches(QKeySequence::SelectNextWord));
        return KeyResult::Moved;
    } else if (event->matches(QKeySequence::MoveToPreviousChar) || event->matches(QKeySequence::SelectPreviousChar)) {
        moveTo(std::max(caret - 1, 0), event->matches(QKeySequence::SelectPreviousChar));
        return KeyResult::Moved;
    } else if (event->matches(QKeySequence::MoveToNextChar) || event->matches(QKeySequence::SelectNextChar)) {
        moveTo(std::min(caret + 1, end), event->matches(QKeySequence::SelectNextChar));
        return KeyResult::Moved;
    } else if (event->matches(QKeySequence::MoveToStartOfLine) || event->matches(QKeySequence::SelectStartOfLine)) {
        moveTo(0, event->matches(QKeySequence::SelectStartOfLine));
        return KeyResult::Moved;
    } else if (event->matches(QKeySequence::MoveToEndOfLine) || event->matches(QKeySequence::SelectEndOfLine)) {
        moveTo(end, event->matches(QKeySequence::SelectEndOfLine));
        return KeyResult::Moved;
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        return KeyResult::Enter;
    } else if (event->key() == Qt::Key_Escape) {
        return KeyResult::Escape;
    } else if (const QString typed = event->text(); !typed.isEmpty() && typed.at(0).isPrint()) {
        insert(typed);
    } else {
        return KeyResult::Ignored;
    }
    return KeyResult::Edited;
}

void LineEdit::paint(FormCanvas& canvas, const QRect& rect, bool focused, const QString& hint) {
    if (!canvas.painter) {
        return;
    }
    canvas.painter->setPen(focused ? canvas.current : canvas.normal);
    canvas.painter->drawRect(rect.adjusted(0, 0, -1, -1));
    const QRect inner = rect.adjusted(3, 0, -3, 0);
    if (content.isEmpty() && !focused) {
        canvas.text(inner, hint, canvas.dim);
        return;
    }
    const int room = inner.width() - 2;
    const int caretX = canvas.textWidth(content.left(caret));
    const int full = canvas.textWidth(content);
    scroll = std::clamp(scroll, std::max(0, caretX - room), std::max(0, std::min(caretX, full - room)));
    canvas.painter->save();
    canvas.painter->setClipRect(inner, Qt::IntersectClip);
    const int origin = inner.x() - scroll;
    if (focused && hasSelection()) {
        const int from = canvas.textWidth(content.left(std::min(anchor, caret)));
        const int to = canvas.textWidth(content.left(std::max(anchor, caret)));
        canvas.painter->fillRect(QRect(origin + from, rect.y() + 2, to - from + 1, rect.height() - 4), canvas.selected);
    }
    if (full <= room) {
        canvas.text(inner, content, canvas.current);
    } else {
        canvas.painter->setFont(canvas.regular);
        canvas.painter->setPen(canvas.current);
        canvas.painter->drawText(
            QRect(origin, inner.y(), full + 2, inner.height()), Qt::AlignLeft | Qt::AlignVCenter, content
        );
    }
    if (focused) {
        canvas.painter->setPen(canvas.current);
        canvas.painter->drawLine(origin + caretX + 1, rect.y() + 2, origin + caretX + 1, rect.bottom() - 2);
    }
    canvas.painter->restore();
}

void LineEdit::pressAt(const QRect& rect, QPoint pos, const QFontMetrics& metrics) {
    const int target = pos.x() - (rect.x() + 3) + scroll;
    int best = 0;
    for (int position = 1; position <= content.size(); ++position) {
        if (std::abs(metrics.horizontalAdvance(content.left(position)) - target)
            < std::abs(metrics.horizontalAdvance(content.left(best)) - target)) {
            best = position;
        }
    }
    caret = best;
    anchor = best;
}

void LineEdit::insert(const QString& text) {
    removeSelection();
    const QString taken = text.left(std::max(0, maxLength - static_cast<int>(content.size())));
    content.insert(caret, taken);
    caret += static_cast<int>(taken.size());
    anchor = caret;
}

void LineEdit::erase(int from, int to) {
    content.remove(from, to - from);
    caret = from;
    anchor = from;
}

void LineEdit::moveTo(int position, bool select) {
    caret = position;
    if (!select) {
        anchor = position;
    }
}

bool LineEdit::removeSelection() {
    if (!hasSelection()) {
        return false;
    }
    erase(std::min(anchor, caret), std::max(anchor, caret));
    return true;
}

int LineEdit::wordBefore(int position) const {
    while (position > 0 && !IsWordCharacter(content.at(position - 1))) {
        --position;
    }
    while (position > 0 && IsWordCharacter(content.at(position - 1))) {
        --position;
    }
    return position;
}

int LineEdit::wordAfter(int position) const {
    const int end = static_cast<int>(content.size());
    while (position < end && !IsWordCharacter(content.at(position))) {
        ++position;
    }
    while (position < end && IsWordCharacter(content.at(position))) {
        ++position;
    }
    return position;
}

}  // namespace Ui
