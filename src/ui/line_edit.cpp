#include "ui/line_edit.h"

#include "ui/form_canvas.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QPainter>
#include <QTextBoundaryFinder>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <initializer_list>

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
    QKeySequence::MoveToStartOfDocument,
    QKeySequence::MoveToEndOfDocument,
    QKeySequence::SelectPreviousChar,
    QKeySequence::SelectNextChar,
    QKeySequence::SelectPreviousWord,
    QKeySequence::SelectNextWord,
    QKeySequence::SelectStartOfLine,
    QKeySequence::SelectEndOfLine,
    QKeySequence::SelectStartOfDocument,
    QKeySequence::SelectEndOfDocument,
};

QString WholeCharacters(QString text) {
    if (!text.isEmpty() && text.back().isHighSurrogate()) {
        text.chop(1);
    }
    return text;
}

bool AnyOf(const QKeyEvent* event, std::initializer_list<QKeySequence::StandardKey> keys) {
    return std::any_of(keys.begin(), keys.end(), [event](QKeySequence::StandardKey key) {
        return event->matches(key);
    });
}

}  // namespace

LineEdit::LineEdit(int limit)
    : maxLength(limit) { }

void LineEdit::setText(const QString& text) {
    content = WholeCharacters(text.left(maxLength));
    caret = static_cast<int>(content.size());
    anchor = caret;
}

QString LineEdit::selectedText() const {
    const int from = std::min(anchor, caret);
    return content.mid(from, std::abs(caret - anchor));
}

bool LineEdit::takes(const QKeyEvent* event) {
    const Qt::KeyboardModifiers modifiers =
        event->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier | Qt::GroupSwitchModifier);
    return modifiers == Qt::NoModifier
        || std::any_of(kEditKeys.cbegin(), kEditKeys.cend(), [event](QKeySequence::StandardKey key) {
               return event->matches(key);
           });
}

LineEdit::KeyResult LineEdit::press(const QKeyEvent* event) {
    const int end = static_cast<int>(content.size());
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
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
            erase(caret, after(caret));
        }
    } else if (event->key() == Qt::Key_Backspace && modifiers == Qt::NoModifier) {
        if (!removeSelection()) {
            erase(before(caret), caret);
        }
    } else if (AnyOf(event, {QKeySequence::MoveToPreviousWord, QKeySequence::SelectPreviousWord})) {
        moveTo(wordBefore(caret), event->matches(QKeySequence::SelectPreviousWord));
        return KeyResult::Moved;
    } else if (AnyOf(event, {QKeySequence::MoveToNextWord, QKeySequence::SelectNextWord})) {
        moveTo(wordAfter(caret), event->matches(QKeySequence::SelectNextWord));
        return KeyResult::Moved;
    } else if (AnyOf(event, {QKeySequence::MoveToPreviousChar, QKeySequence::SelectPreviousChar})) {
        moveByLetter(false, event->matches(QKeySequence::SelectPreviousChar));
        return KeyResult::Moved;
    } else if (AnyOf(event, {QKeySequence::MoveToNextChar, QKeySequence::SelectNextChar})) {
        moveByLetter(true, event->matches(QKeySequence::SelectNextChar));
        return KeyResult::Moved;
    } else if (AnyOf(
                   event,
                   {QKeySequence::MoveToStartOfLine,
                    QKeySequence::SelectStartOfLine,
                    QKeySequence::MoveToStartOfDocument,
                    QKeySequence::SelectStartOfDocument}
               )) {
        moveTo(0, AnyOf(event, {QKeySequence::SelectStartOfLine, QKeySequence::SelectStartOfDocument}));
        return KeyResult::Moved;
    } else if (AnyOf(
                   event,
                   {QKeySequence::MoveToEndOfLine,
                    QKeySequence::SelectEndOfLine,
                    QKeySequence::MoveToEndOfDocument,
                    QKeySequence::SelectEndOfDocument}
               )) {
        moveTo(end, AnyOf(event, {QKeySequence::SelectEndOfLine, QKeySequence::SelectEndOfDocument}));
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
    const int lowest = std::max(0, caretX - room);
    scroll = std::clamp(scroll, lowest, std::max(lowest, std::min(caretX, full - room)));
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
    for (int position = 0; position < content.size();) {
        position = after(position);
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
    const QString taken = WholeCharacters(text.left(std::max(0, maxLength - static_cast<int>(content.size()))));
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

int LineEdit::before(int position) const {
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, content);
    finder.setPosition(position);
    return std::max(0, static_cast<int>(finder.toPreviousBoundary()));
}

int LineEdit::after(int position) const {
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, content);
    finder.setPosition(position);
    const qsizetype next = finder.toNextBoundary();
    return next < 0 ? static_cast<int>(content.size()) : static_cast<int>(next);
}

int LineEdit::wordBefore(int position) const {
    QTextBoundaryFinder finder(QTextBoundaryFinder::Word, content);
    finder.setPosition(position);
    qsizetype found = finder.toPreviousBoundary();
    while (found > 0 && !finder.boundaryReasons().testFlag(QTextBoundaryFinder::StartOfItem)) {
        found = finder.toPreviousBoundary();
    }
    return std::max(0, static_cast<int>(found));
}

int LineEdit::wordAfter(int position) const {
    QTextBoundaryFinder finder(QTextBoundaryFinder::Word, content);
    finder.setPosition(position);
    qsizetype found = finder.toNextBoundary();
    while (found >= 0 && found < content.size() && !finder.boundaryReasons().testFlag(QTextBoundaryFinder::EndOfItem)) {
        found = finder.toNextBoundary();
    }
    return found < 0 ? static_cast<int>(content.size()) : static_cast<int>(found);
}

void LineEdit::moveByLetter(bool forward, bool select) {
    if (hasSelection() && !select) {
        moveTo(forward ? std::max(anchor, caret) : std::min(anchor, caret), false);
        return;
    }
    moveTo(forward ? after(caret) : before(caret), select);
}

}  // namespace Ui
