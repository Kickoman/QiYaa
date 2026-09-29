#include "audio/audio_engine.h"
#include "core/player.h"
#include "skins/skin.h"
#include "ui/main_window.h"
#include "yandex/api_client.h"
#include "yandex/library.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QImage>
#include <QList>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QScreen>
#include <QTest>

#include <functional>
#include <memory>

class TestMainWindow : public QObject {
    Q_OBJECT
private:
    Skins::Skin skin;
    QNetworkAccessManager networkManager;
    std::unique_ptr<Yandex::ApiClient> api;
    std::unique_ptr<Yandex::Library> library;
    std::unique_ptr<Audio::AudioEngine> engine;
    std::unique_ptr<Core::Player> player;
    std::unique_ptr<Ui::MainWindow> window;
    QRect screen;

    void send(QEvent::Type type, QPoint local, Qt::MouseButtons buttons) {
        const QPointF global = window->mapToGlobal(QPointF(local));
        QMouseEvent mouseEvent(
            type, QPointF(local), global, type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
            buttons, Qt::NoModifier
        );
        QCoreApplication::sendEvent(window.get(), &mouseEvent);
    }

private Q_SLOTS:
    void initTestCase() {
        skin = Skins::Skin::BuiltinBase();
        QVERIFY(skin.isValid());
        api = std::make_unique<Yandex::ApiClient>(&networkManager);
        library = std::make_unique<Yandex::Library>(api.get());
        engine = std::make_unique<Audio::AudioEngine>();
        player = std::make_unique<Core::Player>(library.get(), engine.get());
        window = std::make_unique<Ui::MainWindow>(player.get(), &skin);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        screen = QGuiApplication::primaryScreen()->availableGeometry();
        QVERIFY(screen.width() >= 400);
    }

    void restoredPositionIsClampedOnScreen() {
        window->placeAt(QPoint(-500, 5000));
        QCOMPARE(window->pos(), QPoint(screen.left(), screen.bottom() + 1 - window->height()));
        window->placeAt(QPoint(100, 100));
        QCOMPARE(window->pos(), QPoint(100, 100));
    }

    void dragMovesWindow() {
        window->placeAt(QPoint(100, 100));
        send(QEvent::MouseButtonPress, {100, 5}, Qt::LeftButton);
        // Local coords of the move are relative to the old position; global is what matters.
        QMouseEvent move(
            QEvent::MouseMove, QPointF(150, 45), QPointF(250, 145), Qt::NoButton, Qt::LeftButton,
            Qt::NoModifier
        );
        QCoreApplication::sendEvent(window.get(), &move);
        QCOMPARE(window->pos(), QPoint(150, 140));
        QMouseEvent release(
            QEvent::MouseButtonRelease, QPointF(100, 5), QPointF(250, 145), Qt::LeftButton,
            Qt::NoButton, Qt::NoModifier
        );
        QCoreApplication::sendEvent(window.get(), &release);
    }

    void dragSnapsToScreenEdgeAndCannotLeaveScreen() {
        window->placeAt(QPoint(100, 100));
        send(QEvent::MouseButtonPress, {100, 5}, Qt::LeftButton);
        QMouseEvent farMove(
            QEvent::MouseMove, QPointF(0, 0), QPointF(5000, 205), Qt::NoButton, Qt::LeftButton,
            Qt::NoModifier
        );
        QCoreApplication::sendEvent(window.get(), &farMove);
        QCOMPARE(window->pos().x(), screen.right() + 1 - window->width());
        // Near the left edge (within 15 px): snaps to 0.
        QMouseEvent nearLeft(
            QEvent::MouseMove, QPointF(0, 0), QPointF(screen.left() + 110, 205), Qt::NoButton,
            Qt::LeftButton, Qt::NoModifier
        );
        QCoreApplication::sendEvent(window.get(), &nearLeft);
        QCOMPARE(window->pos().x(), screen.left());
        QMouseEvent release(
            QEvent::MouseButtonRelease, QPointF(100, 5), QPointF(110, 205), Qt::LeftButton,
            Qt::NoButton, Qt::NoModifier
        );
        QCoreApplication::sendEvent(window.get(), &release);
    }

    void clickingShuffleToggles() {
        QVERIFY(!player->shuffle());
        const QPoint shuffleButton(164 + 20, 89 + 7);
        send(QEvent::MouseButtonPress, shuffleButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, shuffleButton, Qt::NoButton);
        QVERIFY(player->shuffle());
    }

    void shuffleButtonShowsOffDuringAWave() {  // spec WAVE-10
        const QRect button(164, 89, 47, 15);
        auto shuffleButton = [&] { return window->grab(button).toImage(); };
        player->setShuffle(true);
        const QImage on = shuffleButton();
        player->setShuffle(false);
        const QImage off = shuffleButton();
        QVERIFY(on != off);
        player->setShuffle(true);
        player->setQueue({}, "Wave", false, [](std::function<void(const QList<Yandex::Track>&)>) {
        });
        QCOMPARE(shuffleButton(), off);
        player->setQueue({}, "Plain", false);
        QCOMPARE(shuffleButton(), on);
    }

    void volumeSliderFollowsMouse() {
        send(QEvent::MouseButtonPress, {107 + 7, 62}, Qt::LeftButton);  // far left of the slider
        QCOMPARE(window->volume(), 0);
        send(QEvent::MouseMove, {107 + 61, 62}, Qt::LeftButton);  // far right
        QCOMPARE(window->volume(), 100);
        send(QEvent::MouseButtonRelease, {107 + 61, 62}, Qt::NoButton);
    }

    void doubleSizeKeepsWindowOnScreen() {
        window->placeAt(QPoint(screen.right() + 1 - window->width(), 100));
        window->setScale(2);
        QCOMPARE(window->size(), QSize(550, 232));
        QVERIFY(window->pos().x() + window->width() <= screen.right() + 1);
        window->setScale(1);
    }

    void fractionalScaleSizesTheWindowAndMapsClicksBack() {
        window->placeAt(QPoint(0, 0));
        window->setScale(1.5);
        QCOMPARE(window->scale(), 1.5);
        QCOMPARE(window->size(), QSize(413, 174));
        // Clicks map back to skin coordinates: Shuffle at skin (184, 96).
        const bool before = player->shuffle();
        const QPoint shuffleButton(qRound(184 * 1.5), qRound(96 * 1.5));
        send(QEvent::MouseButtonPress, shuffleButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, shuffleButton, Qt::NoButton);
        QCOMPARE(player->shuffle(), !before);
        const QImage image = window->grab().toImage();
        QCOMPARE(image.size(), QSize(413, 174));
        window->setScale(1.3333);  // rounded to 0.05 steps
        QCOMPARE(window->scale(), 1.35);
        window->setScale(1);
    }

    void cleanupTestCase() { window.reset(); }
};

QTEST_MAIN(TestMainWindow)
#include "main_window_test.moc"
