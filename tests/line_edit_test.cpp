#include "ui/line_edit.h"

#include <QClipboard>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QTest>

namespace {

Ui::LineEdit::KeyResult Press(Ui::LineEdit& edit, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    const QKeyEvent event(QEvent::KeyPress, key, modifiers);
    return edit.press(&event);
}

void Type(Ui::LineEdit& edit, const QString& text) {
    for (const QChar character : text) {
        const QKeyEvent event(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(character));
        edit.press(&event);
    }
}

bool Takes(int key, Qt::KeyboardModifiers modifiers) {
    const QKeyEvent event(QEvent::ShortcutOverride, key, modifiers);
    return Ui::LineEdit::takes(&event);
}

}  // namespace

class TestLineEdit : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void typingGoesInAtTheCaretUpToTheLimit() {
        Ui::LineEdit edit(5);
        Type(edit, QStringLiteral("abc"));
        Press(edit, Qt::Key_Left);
        Press(edit, Qt::Key_Left);
        Type(edit, QStringLiteral("X"));
        QCOMPARE(edit.text(), QStringLiteral("aXbc"));
        Type(edit, QStringLiteral("123"));
        QCOMPARE(edit.text(), QStringLiteral("aX1bc"));
        QCOMPARE(edit.cursor(), 3);
    }

    void homeEndDeleteAndBackspace() {
        Ui::LineEdit edit(100);
        edit.setText(QStringLiteral("abc"));
        Press(edit, Qt::Key_Home);
        QCOMPARE(Press(edit, Qt::Key_Delete), Ui::LineEdit::KeyResult::Edited);
        QCOMPARE(edit.text(), QStringLiteral("bc"));
        Press(edit, Qt::Key_End);
        Press(edit, Qt::Key_Backspace);
        QCOMPARE(edit.text(), QStringLiteral("b"));
        Press(edit, Qt::Key_Home);
        Press(edit, Qt::Key_Backspace);
        QCOMPARE(edit.text(), QStringLiteral("b"));
    }

    void wordsMoveAndGoWhole() {
        Ui::LineEdit edit(100);
        edit.setText(QStringLiteral("one two three"));
        QCOMPARE(Press(edit, Qt::Key_Left, Qt::ControlModifier), Ui::LineEdit::KeyResult::Moved);
        QCOMPARE(edit.cursor(), 8);
        Press(edit, Qt::Key_Backspace, Qt::ControlModifier);
        QCOMPARE(edit.text(), QStringLiteral("one three"));
        QCOMPARE(edit.cursor(), 4);
        Press(edit, Qt::Key_Delete, Qt::ControlModifier);
        QCOMPARE(edit.text(), QStringLiteral("one "));
    }

    void aSelectionIsReplacedCutAndCopied() {
        Ui::LineEdit edit(100);
        edit.setText(QStringLiteral("hello world"));
        Press(edit, Qt::Key_Left, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(edit.selectedText(), QStringLiteral("world"));
        Type(edit, QStringLiteral("there"));
        QCOMPARE(edit.text(), QStringLiteral("hello there"));
        Press(edit, Qt::Key_A, Qt::ControlModifier);
        Press(edit, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("hello there"));
        QCOMPARE(edit.text(), QStringLiteral("hello there"));
        Press(edit, Qt::Key_X, Qt::ControlModifier);
        QVERIFY(edit.text().isEmpty());
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("hello there"));
    }

    void pastingTakesOneLine() {
        Ui::LineEdit edit(100);
        QGuiApplication::clipboard()->setText(QStringLiteral("кино\n группа   крови"));
        Press(edit, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(edit.text(), QStringLiteral("кино группа крови"));
    }

    void enterAndEscapeAreTheCallers() {
        Ui::LineEdit edit(100);
        QCOMPARE(Press(edit, Qt::Key_Return), Ui::LineEdit::KeyResult::Enter);
        QCOMPARE(Press(edit, Qt::Key_Escape), Ui::LineEdit::KeyResult::Escape);
        QCOMPARE(Press(edit, Qt::Key_F5), Ui::LineEdit::KeyResult::Ignored);
    }

    void aFocusedFieldTakesTheEditingKeysOnly() {
        QVERIFY(Takes(Qt::Key_X, Qt::NoModifier));
        QVERIFY(Takes(Qt::Key_Left, Qt::NoModifier));
        QVERIFY(Takes(Qt::Key_Left, Qt::ControlModifier));
        QVERIFY(Takes(Qt::Key_V, Qt::ControlModifier));
        QVERIFY(Takes(Qt::Key_A, Qt::ControlModifier));
        QVERIFY(!Takes(Qt::Key_D, Qt::ControlModifier));
        QVERIFY(!Takes(Qt::Key_E, Qt::AltModifier));
    }

    void aClickPutsTheCaretAtTheNearestLetter() {
        Ui::LineEdit edit(100);
        edit.setText(QStringLiteral("abcdef"));
        const QFontMetrics metrics{QFont()};
        const QRect rect(10, 0, 300, 15);
        edit.pressAt(rect, QPoint(13 + metrics.horizontalAdvance(QStringLiteral("abc")), 5), metrics);
        QCOMPARE(edit.cursor(), 3);
        QVERIFY(!edit.hasSelection());
    }
};

QTEST_MAIN(TestLineEdit)
#include "line_edit_test.moc"
