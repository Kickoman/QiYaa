// Drives the real main window on the offscreen platform (one 800x600 screen).
#include <QGuiApplication>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QScreen>
#include <QTest>

#include "audio/AudioEngine.h"
#include "core/Player.h"
#include "skin/Skin.h"
#include "ui/MainWindow.h"
#include "yandex/ApiClient.h"

using namespace qiyaa;

class TestMainWindow : public QObject {
    Q_OBJECT
private:
    Skin skin;
    QNetworkAccessManager nam;
    std::unique_ptr<yandex::ApiClient> api;
    std::unique_ptr<audio::AudioEngine> engine;
    std::unique_ptr<Player> player;
    std::unique_ptr<MainWindow> win;
    QRect screen;

    void send(QEvent::Type type, QPoint local, Qt::MouseButtons buttons) {
        const QPointF global = win->mapToGlobal(QPointF(local));
        QMouseEvent e(type, QPointF(local), global,
                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(win.get(), &e);
    }

private Q_SLOTS:
    void initTestCase() {
        skin = Skin::builtinBase();
        QVERIFY(skin.isValid());
        api = std::make_unique<yandex::ApiClient>(&nam);
        engine = std::make_unique<audio::AudioEngine>();
        player = std::make_unique<Player>(api.get(), engine.get());
        win = std::make_unique<MainWindow>(player.get(), &skin);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win.get()));
        screen = QGuiApplication::primaryScreen()->availableGeometry();
        QVERIFY(screen.width() >= 400);
    }

    void restoredPositionIsClampedOnScreen() {
        win->placeAt(QPoint(-500, 5000));
        QCOMPARE(win->pos(), QPoint(screen.left(), screen.bottom() + 1 - win->height()));
        win->placeAt(QPoint(100, 100));
        QCOMPARE(win->pos(), QPoint(100, 100));
    }

    void dragMovesWindow() {
        win->placeAt(QPoint(100, 100));
        send(QEvent::MouseButtonPress, {100, 5}, Qt::LeftButton);
        // Local coords of the move are relative to the old position; global is what matters.
        QMouseEvent move(QEvent::MouseMove, QPointF(150, 45), QPointF(250, 145), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(win.get(), &move);
        QCOMPARE(win->pos(), QPoint(150, 140));
        QMouseEvent up(QEvent::MouseButtonRelease, QPointF(100, 5), QPointF(250, 145), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(win.get(), &up);
    }

    void dragSnapsToScreenEdgeAndCannotLeaveScreen() {
        win->placeAt(QPoint(100, 100));
        send(QEvent::MouseButtonPress, {100, 5}, Qt::LeftButton);
        // Cursor way past the right edge: the window must stop at the edge.
        QMouseEvent far(QEvent::MouseMove, QPointF(0, 0), QPointF(5000, 205), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(win.get(), &far);
        QCOMPARE(win->pos().x(), screen.right() + 1 - win->width());
        // Near the left edge (within 15 px): snaps to 0.
        QMouseEvent nearLeft(QEvent::MouseMove, QPointF(0, 0), QPointF(screen.left() + 110, 205), Qt::NoButton,
                             Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(win.get(), &nearLeft);
        QCOMPARE(win->pos().x(), screen.left());
        QMouseEvent up(QEvent::MouseButtonRelease, QPointF(100, 5), QPointF(110, 205), Qt::LeftButton, Qt::NoButton,
                       Qt::NoModifier);
        QCoreApplication::sendEvent(win.get(), &up);
    }

    void clickingShuffleToggles() {
        QVERIFY(!player->shuffle());
        const QPoint shuffle(164 + 20, 89 + 7);
        send(QEvent::MouseButtonPress, shuffle, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, shuffle, Qt::NoButton);
        QVERIFY(player->shuffle());
    }

    void volumeSliderFollowsMouse() {
        send(QEvent::MouseButtonPress, {107 + 7, 62}, Qt::LeftButton);  // far left of the slider
        QCOMPARE(win->volume(), 0);
        send(QEvent::MouseMove, {107 + 61, 62}, Qt::LeftButton);  // far right
        QCOMPARE(win->volume(), 100);
        send(QEvent::MouseButtonRelease, {107 + 61, 62}, Qt::NoButton);
    }

    void doubleSizeKeepsWindowOnScreen() {
        win->placeAt(QPoint(screen.right() + 1 - win->width(), 100));
        win->setScale(2);
        QCOMPARE(win->size(), QSize(550, 232));
        QVERIFY(win->pos().x() + win->width() <= screen.right() + 1);
        win->setScale(1);
    }

    void cleanupTestCase() { win.reset(); }
};

QTEST_MAIN(TestMainWindow)
#include "test_mainwindow.moc"
