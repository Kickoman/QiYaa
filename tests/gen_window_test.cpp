#include "skins/skin.h"
#include "ui/gen_window.h"
#include "ui/skinned_window.h"

#include <QCoreApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QObject>
#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSignalSpy>
#include <QSize>
#include <QString>
#include <QTest>
#include <QWidget>

namespace {

class Probe : public Ui::GenWindow {
public:
    explicit Probe(const Skins::Skin* skin)
        : GenWindow(skin, QStringLiteral("PROBE")) { }

    QRect content() const { return contentRect(); }

    QRect painted;

protected:
    void paintContent(QPainter&, const QRect& area) override { painted = area; }
};

void SendMouseEvent(
    QWidget* widget,
    QEvent::Type type,
    QPoint local,
    Qt::MouseButton button,
    Qt::MouseButtons buttons
) {
    QMouseEvent event(type, QPointF(local), QPointF(widget->mapToGlobal(local)), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &event);
}

void Click(QWidget* widget, QPoint local) {
    SendMouseEvent(widget, QEvent::MouseButtonPress, local, Qt::LeftButton, Qt::LeftButton);
    SendMouseEvent(widget, QEvent::MouseButtonRelease, local, Qt::LeftButton, Qt::NoButton);
}

}  // namespace

class TestGenWindow : public QObject {
    Q_OBJECT
private:
    Skins::Skin base = Skins::Skin::BuiltinBase();
    Skins::Skin vizor = Skins::Skin::LoadFile(QStringLiteral(":/skins/Vizor1-01.wsz"), &base);

private Q_SLOTS:
    void theGenFrameKeepsItsInsets() {
        Probe window(&base);
        QCOMPARE(window.content(), QRect(11, 20, 256, 82));
        window.grab();
        QCOMPARE(window.painted, window.content());
        window.setSizeSteps({2, 3});
        QCOMPARE(window.skinSize(), QSize(325, 203));
        QCOMPARE(window.content(), QRect(11, 20, 306, 169));
    }

    void withoutAGenSheetOfItsOwnTheFrameComesFromThePlaylist() {
        QCOMPARE(vizor.genFrame(), Skins::Skin::GenFrame::Playlist);
        Probe window(&vizor);
        QCOMPARE(window.content(), QRect(12, 20, 243, 78));
        window.grab();
        QCOMPARE(window.painted, window.content());
        window.setSizeSteps({2, 3});
        QCOMPARE(window.skinSize(), QSize(325, 203));
        QCOMPARE(window.content(), QRect(12, 20, 293, 165));
    }

    void aNewSkinChangesTheFrameAtOnce() {
        Probe window(&base);
        window.setSkin(&vizor);
        QCOMPARE(window.content(), QRect(12, 20, 243, 78));
        window.setSkin(&base);
        QCOMPARE(window.content(), QRect(11, 20, 256, 82));
    }

    void bothFramesCloseAndResizeAtTheSamePlaces_data() {
        QTest::addColumn<bool>("fromPlaylist");
        QTest::newRow("gen") << false;
        QTest::newRow("playlist") << true;
    }
    void bothFramesCloseAndResizeAtTheSamePlaces() {
        QFETCH(bool, fromPlaylist);
        Probe window(fromPlaylist ? &vizor : &base);
        QSignalSpy closed(&window, &Ui::SkinnedWindow::closeRequested);
        const QSize size = window.skinSize();
        Click(&window, {size.width() - 21 + 4, 3 + 4});
        QCOMPARE(closed.count(), 0);
        QCOMPARE(window.sizeSteps(), QSize(0, 0));
        Click(&window, {size.width() - 11 + 4, 3 + 4});
        QCOMPARE(closed.count(), 1);
        const QPoint grip(size.width() - 5, size.height() - 5);
        const QPoint moved = grip + QPoint(25, 29);
        SendMouseEvent(&window, QEvent::MouseButtonPress, grip, Qt::LeftButton, Qt::LeftButton);
        SendMouseEvent(&window, QEvent::MouseMove, moved, Qt::NoButton, Qt::LeftButton);
        SendMouseEvent(&window, QEvent::MouseButtonRelease, moved, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(window.sizeSteps(), QSize(1, 1));
        QCOMPARE(closed.count(), 1);
    }
};

QTEST_MAIN(TestGenWindow)
#include "gen_window_test.moc"
