// The full window set on the offscreen platform: docking, scale, playlist, EQ.
#include <QApplication>
#include <QBuffer>
#include <QLabel>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>

#include "app/App.h"
#include "ui/EqualizerWindow.h"
#include "MockHttpServer.h"
#include "core/CoverCache.h"
#include "ui/LoginDialog.h"
#include "ui/MilkdropWindow.h"
#include "ui/NowPlayingWindow.h"
#include "ui/MainWindow.h"
#include "ui/PlaylistWindow.h"

using namespace qiyaa;
using yandex::Track;

namespace {

void mouse(QWidget* w, QEvent::Type type, QPoint local, QPoint global, Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent e(type, QPointF(local), QPointF(global), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &e);
}

// Press at `local` (window pixels) and move the cursor by `delta`.
void drag(QWidget* w, QPoint local, QPoint delta) {
    const QPoint g = w->mapToGlobal(local);
    mouse(w, QEvent::MouseButtonPress, local, g, Qt::LeftButton, Qt::LeftButton);
    // Local coordinates are relative to the window's position at press time,
    // which is what Qt reports while the window follows the cursor.
    mouse(w, QEvent::MouseMove, local + delta, g + delta, Qt::NoButton, Qt::LeftButton);
    mouse(w, QEvent::MouseButtonRelease, local + delta, g + delta, Qt::LeftButton, Qt::NoButton);
}

void click(QWidget* w, QPoint local) {
    const QPoint g = w->mapToGlobal(local);
    mouse(w, QEvent::MouseButtonPress, local, g, Qt::LeftButton, Qt::LeftButton);
    mouse(w, QEvent::MouseButtonRelease, local, g, Qt::LeftButton, Qt::NoButton);
}

QList<Track> tracks(int n) {
    QList<Track> out;
    for (int i = 0; i < n; ++i) {
        Track t;
        t.id = QString::number(i + 1);
        t.title = QStringLiteral("Track %1").arg(i + 1);
        t.artists << QStringLiteral("Artist");
        t.durationMs = 60000 + i * 1000;
        out << t;
    }
    return out;
}

}  // namespace

class TestWindows : public QObject {
    Q_OBJECT
private:
    std::unique_ptr<App> app;
    MainWindow* main = nullptr;
    EqualizerWindow* eq = nullptr;
    PlaylistWindow* pl = nullptr;

    // Scale tests need the stack to fit on screen (see tests/CMakeLists.txt).
    void requireBigScreen() {
        if (QGuiApplication::primaryScreen()->availableGeometry().height() < 1200)
            QSKIP("needs a 2560x1440 virtual screen");
    }

private Q_SLOTS:
    void init() {
        App::Options o;
        o.offline = true;
        o.audio = false;
        o.readOnlySettings = true;
        o.mediaIntegration = false;
        app = std::make_unique<App>(o);
        app->start();
        main = app->mainWindow();
        eq = app->equalizerWindow();
        pl = app->playlistWindow();
        QVERIFY(QTest::qWaitForWindowExposed(main));
    }
    void cleanup() { app.reset(); }

    void defaultLayoutIsStacked() {
        QVERIFY(eq->isVisible() && pl->isVisible());
        QCOMPARE(eq->pos(), main->pos() + QPoint(0, main->height()));
        QCOMPARE(pl->pos(), eq->pos() + QPoint(0, eq->height()));
        QCOMPARE(main->dockedWindows().size(), 2);
    }

    void mainDragsDockedWindows() {
        const QPoint eqOffset = eq->pos() - main->pos();
        const QPoint plOffset = pl->pos() - main->pos();
        drag(main, {100, 5}, {40, 20});
        QCOMPARE(eq->pos() - main->pos(), eqOffset);
        QCOMPARE(pl->pos() - main->pos(), plOffset);
    }

    void equalizerDetachesAndSnapsBack() {
        const QPoint start = eq->pos();
        drag(eq, {100, 5}, {300, 0});            // pull it away to the right
        QCOMPARE(eq->pos(), start + QPoint(300, 0));
        QVERIFY(!main->dockedWindows().contains(eq));
        drag(eq, {100, 5}, {-292, 0});           // within 15 px of the old spot: snaps
        QCOMPARE(eq->pos(), start);
    }

    void scalingKeepsTheStack() {
        requireBigScreen();
        app->setScale(1.5);
        QCOMPARE(main->size(), QSize(413, 174));
        QCOMPARE(eq->pos(), main->pos() + QPoint(0, main->height()));
        QCOMPARE(pl->pos(), eq->pos() + QPoint(0, eq->height()));
    }

    void scalingRoundTripsStayDocked() {
        requireBigScreen();
        for (double s : {1.25, 1.75, 1.35, 1.0, 2.5, 1.5}) {
            app->setScale(s);
            QCOMPARE(eq->pos(), main->pos() + QPoint(0, main->height()));
            QCOMPARE(pl->pos(), eq->pos() + QPoint(0, eq->height()));
            QCOMPARE(main->dockedWindows().size(), 2);
        }
    }

    void hugeScaleKeepsEveryWindowReachable() {
        // Stack at 400% is 1856 px tall: taller than the 1440 px test screen.
        app->setScale(4.0);
        const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        for (QWidget* w : {static_cast<QWidget*>(main), static_cast<QWidget*>(eq), static_cast<QWidget*>(pl)}) {
            const QRect r = w->frameGeometry();
            QVERIFY2(screen.contains(r.topLeft()), qPrintable(w->windowTitle()));
            if (r.width() <= screen.width() && r.height() <= screen.height()) QVERIFY(screen.contains(r));
        }
    }

    void hiddenWindowFollowsScale() {
        requireBigScreen();
        app->setEqualizerVisible(false);
        app->setScale(2.0);
        app->setEqualizerVisible(true);
        QCOMPARE(eq->pos(), main->pos() + QPoint(0, main->height()));
    }

    void newQueueClearsSelection() {
        app->player()->setQueue(tracks(10), "A", false);
        click(pl, {60, 20 + 3 + 6});
        QCOMPARE(pl->selection().size(), 1);
        app->player()->setQueue(tracks(10), "B", false);
        QVERIFY(pl->selection().isEmpty());
        QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QCoreApplication::sendEvent(pl, &del);
        QCOMPARE(app->player()->playlist().size(), 10);  // nothing was selected
    }

    void shiftArrowsGrowTheRange() {
        app->player()->setQueue(tracks(10), "A", false);
        click(pl, {60, 20 + 3 + 6});  // row 0
        for (int i = 0; i < 3; ++i) {
            QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::ShiftModifier);
            QCoreApplication::sendEvent(pl, &down);
        }
        QCOMPARE(pl->selection(), (QSet<int>{0, 1, 2, 3}));
    }

    void smoothScrollWheelChangesVolume() {
        main->setVolume(50);
        for (int i = 0; i < 3; ++i) {  // three 40-unit touchpad deltas = one notch
            QWheelEvent w(QPointF(150, 60), main->mapToGlobal(QPointF(150, 60)), QPoint(), QPoint(0, 40), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(main, &w);
        }
        QCOMPARE(main->volume(), 54);
    }

    void eqButtonTogglesWindow() {
        click(main, {219 + 10, 58 + 5});
        QVERIFY(!eq->isVisible());
        click(main, {219 + 10, 58 + 5});
        QVERIFY(eq->isVisible());
    }

    void playlistShowsQueueAndPlaysOnDoubleClick() {
        app->player()->setQueue(tracks(50), "Test", false);
        QCOMPARE(pl->visibleRows(), 13);  // default height 232: (232-58)/13
        const QPoint row3(60, 20 + 3 + 2 * 13 + 6);
        QCOMPARE(pl->rowAt(row3), 2);
        // Double click plays that row (no audio device here, but the index moves).
        QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(row3), pl->mapToGlobal(QPointF(row3)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(pl, &dbl);
        QCOMPARE(app->player()->currentIndex(), 2);
    }

    void playlistScrollsAndResizes() {
        app->player()->setQueue(tracks(50), "Test", false);
        QWheelEvent wheel(QPointF(60, 60), pl->mapToGlobal(QPointF(60, 60)), QPoint(), QPoint(0, -120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(pl, &wheel);
        QCOMPARE(pl->scrollOffset(), 3);
        // Drag the resize grip by one step to the right and two down.
        const QPoint grip(pl->width() - 5, pl->height() - 5);
        drag(pl, grip, {25, 58});
        QCOMPARE(pl->sizeSteps(), QSize(1, 6));
        QCOMPARE(pl->size(), QSize(300, 290));
    }

    void playlistSelectionAndDelete() {
        app->player()->setQueue(tracks(5), "Test", false);
        click(pl, {60, 20 + 3 + 6});  // row 0
        QKeyEvent shiftDown(QEvent::KeyPress, Qt::Key_Down, Qt::ShiftModifier);
        QCoreApplication::sendEvent(pl, &shiftDown);
        QCOMPARE(pl->selection().size(), 2);
        QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QCoreApplication::sendEvent(pl, &del);
        QCOMPARE(app->player()->playlist().size(), 3);
        QCOMPARE(app->player()->playlist().first().title, QStringLiteral("Track 3"));
    }

    void eqSliderDrag() {
        QSignalSpy changed(eq, &EqualizerWindow::settingsChanged);
        // Band 60 Hz at x=78, slider top y=38, 51 px travel: top = +12 dB.
        const QPoint top(78 + 7, 38 + 5);
        mouse(eq, QEvent::MouseButtonPress, top, eq->mapToGlobal(top), Qt::LeftButton, Qt::LeftButton);
        mouse(eq, QEvent::MouseButtonRelease, top, eq->mapToGlobal(top), Qt::LeftButton, Qt::NoButton);
        QVERIFY(changed.count() >= 1);
        QCOMPARE(eq->settings().bandsDb[0], 12.0);
        // ON button turns the EQ off.
        click(eq, {14 + 5, 18 + 5});
        QVERIFY(!eq->settings().enabled);
    }

    void visualizerAndTimeModesToggle() {
        const auto before = main->visMode();
        click(main, {24 + 10, 43 + 5});
        QVERIFY(main->visMode() != before);
        const bool remaining = main->showsRemainingTime();
        click(main, {39 + 20, 26 + 5});
        QCOMPARE(main->showsRemainingTime(), !remaining);
    }

    void loginDialogFitsItsText() {
        QNetworkAccessManager nam;
        // Unreachable OAuth server: the device flow fails fast with a long message.
        LoginDialog dlg(&nam, nullptr, QStringLiteral("http://127.0.0.1:1"));
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        auto allTextFits = [&dlg] {
            for (QLabel* l : dlg.findChildren<QLabel*>()) {
                if (!l->isVisible() || l->text().isEmpty()) continue;
                const int need = l->wordWrap() ? l->heightForWidth(l->width()) : l->sizeHint().height();
                if (l->height() < need) {
                    qWarning("label %s: %d < %d", qPrintable(l->text().left(30)), l->height(), need);
                    return false;
                }
            }
            return true;
        };
        // What the window manager did on GNOME: squeeze it to a small height.
        dlg.resize(dlg.width(), 150);
        QApplication::processEvents();
        QVERIFY(allTextFits());
        QVERIFY(QTest::qWaitFor([&] {
            for (QLabel* l : dlg.findChildren<QLabel*>())
                if (l->text().contains(QStringLiteral("не удался"))) return true;
            return false;
        }, 5000));
        QApplication::processEvents();
        QVERIFY(allTextFits());
        dlg.resize(dlg.width(), 150);
        QApplication::processEvents();
        QVERIFY(allTextFits());
    }

    void shadeModesKeepTheStack() {
        app->player()->setQueue(tracks(3), "A", false);
        const QPoint mainPos = main->pos();
        main->setShaded(true);
        QCOMPARE(main->size(), QSize(275, 14));
        QCOMPARE(eq->pos(), mainPos + QPoint(0, 14));           // pulled up
        QCOMPARE(pl->pos(), eq->pos() + QPoint(0, eq->height()));
        eq->setShaded(true);
        QCOMPARE(eq->height(), 14);
        QCOMPARE(pl->pos(), mainPos + QPoint(0, 28));
        pl->setShaded(true);
        QCOMPARE(pl->size(), QSize(275, 14));
        if (!qEnvironmentVariableIsEmpty("QIYAA_TEST_SHOTS"))
            app->snapshot().save(qEnvironmentVariable("QIYAA_TEST_SHOTS") + "/shaded.png");
        main->setShaded(false);
        eq->setShaded(false);
        pl->setShaded(false);
        QCOMPARE(eq->pos(), mainPos + QPoint(0, 116));
        QCOMPARE(pl->pos(), mainPos + QPoint(0, 232));
        QCOMPARE(pl->size(), QSize(275, 232));
    }

    void shadeLeavesWindowsOthersHold() {
        // Main and EQ side by side, playlist under the EQ.
        const QPoint m = main->pos();
        eq->move(m + QPoint(275, 0));
        pl->move(m + QPoint(275, 116));
        main->setShaded(true);
        QCOMPARE(eq->pos(), m + QPoint(275, 0));
        QCOMPARE(pl->pos(), m + QPoint(275, 116));
        main->setShaded(false);
        QCOMPARE(pl->pos(), m + QPoint(275, 116));
    }

    void hiddenWindowFollowsShade() {
        app->setPlaylistVisible(false);
        main->setShaded(true);
        app->setPlaylistVisible(true);
        QCOMPARE(eq->pos(), main->pos() + QPoint(0, 14));
        QCOMPARE(pl->pos(), eq->pos() + QPoint(0, eq->height()));
    }

    void positionsAreFinalWhenShadeChanges() {
        // The app saves positions on shadeChanged.
        QPoint eqAtSignal;
        connect(main, &SkinnedWindow::shadeChanged, this, [&] { eqAtSignal = eq->pos(); });
        main->setShaded(true);
        QCOMPARE(eqAtSignal, main->pos() + QPoint(0, 14));
    }

    void playlistScrollIsValidAfterUnshade() {
        app->player()->setQueue(tracks(40), "A", false);
        pl->setShaded(true);
        app->player()->playIndex(39);  // scrolls the one-row shaded view to row 39
        pl->setShaded(false);
        QCOMPARE(pl->scrollOffset(), 40 - 13);  // 13 rows fit the default height; last row at the bottom
    }

    void unshadingAtTheBottomStaysOnScreen() {
        main->setShaded(true);
        eq->setShaded(true);
        pl->setShaded(true);
        const QRect screen = main->screen()->availableGeometry();
        const int y = screen.y() + screen.height() - 42;  // the shaded stack sits on the bottom edge
        main->move(main->x(), y);
        eq->move(main->x(), y + 14);
        pl->move(main->x(), y + 28);
        main->setShaded(false);
        QVERIFY(screen.contains(pl->frameGeometry()));
        QCOMPARE(eq->pos(), main->pos() + QPoint(0, 116));
        QCOMPARE(pl->pos(), eq->pos() + QPoint(0, 14));
    }

    void noPlaybackAfterShutDown() {
        app->player()->setQueue(tracks(3), "A", false);
        app->player()->shutDown();
        app->player()->playIndex(2);
        QCOMPARE(app->player()->currentIndex(), 0);
    }

    void milkdropWindowFromTheMenu() {
        MilkdropWindow* md = app->milkdropWindow();
#if defined(QIYAA_HAVE_MILKDROP)
        QVERIFY(md);
        QVERIFY(!md->isVisible());  // off by default
        app->setMilkdropVisible(true);
        QVERIFY(md->isVisible());
        QCOMPARE(md->pos(), main->pos() + QPoint(main->width(), main->height()));  // right of the equalizer
        QVERIFY(md->presets().size() >= 50);
        if (!qEnvironmentVariableIsEmpty("QIYAA_TEST_SHOTS"))
            app->snapshot().save(qEnvironmentVariable("QIYAA_TEST_SHOTS") + "/milkdrop-window.png");
        app->setMilkdropVisible(false);
        QVERIFY(!md->isVisible());
#else
        QVERIFY(!md);
        app->setMilkdropVisible(true);  // no-op
#endif
    }

    void doubleClickTitleShades() {
        QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(100, 5), main->mapToGlobal(QPointF(100, 5)), Qt::LeftButton,
                        Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(main, &dbl);
        QVERIFY(main->isShaded());
        QCoreApplication::sendEvent(main, &dbl);
        QVERIFY(!main->isShaded());
    }

    void eqShadeSlidersDriveMainVolume() {
        eq->setShaded(true);
        const QPoint vol(61 + 96, 7);  // right end of the mini volume slider
        mouse(eq, QEvent::MouseButtonPress, vol, eq->mapToGlobal(vol), Qt::LeftButton, Qt::LeftButton);
        mouse(eq, QEvent::MouseButtonRelease, vol, eq->mapToGlobal(vol), Qt::LeftButton, Qt::NoButton);
        QCOMPARE(main->volume(), 100);
        const QPoint bal(164, 7);      // left end of the mini balance slider
        mouse(eq, QEvent::MouseButtonPress, bal, eq->mapToGlobal(bal), Qt::LeftButton, Qt::LeftButton);
        mouse(eq, QEvent::MouseButtonRelease, bal, eq->mapToGlobal(bal), Qt::LeftButton, Qt::NoButton);
        QCOMPARE(main->balance(), -100);
    }

    void mainShadeTransportWorks() {
        app->player()->setQueue(tracks(3), "A", false);
        main->setShaded(true);
        click(main, {204 + 4, 6});  // mini "next"
        QCOMPARE(app->player()->currentIndex(), 1);
        click(main, {169 + 3, 6});  // mini "previous"
        QCOMPARE(app->player()->currentIndex(), 0);
    }

    void nowPlayingShowsCoverAndDetails() {
        MockHttpServer server;
        QImage red(64, 64, QImage::Format_RGB32);
        red.fill(Qt::red);
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        red.save(&buf, "PNG");
        server.on("GET", "/cover/400x400", [png](const MockRequest&) { return MockResponse{200, png}; });

        QList<Track> list = tracks(1);
        list[0].title = QStringLiteral("Группа крови");
        list[0].artists = {QStringLiteral("Кино")};
        list[0].albumTitle = QStringLiteral("Группа крови");
        list[0].year = 1988;
        list[0].coverUri = server.baseUrl() + QStringLiteral("/cover/%%");
        app->player()->setQueue(list, "A", false);
        app->setNowPlayingVisible(true);
        NowPlayingWindow* np = app->nowPlayingWindow();
        QVERIFY(QTest::qWaitForWindowExposed(np));
        QVERIFY(QTest::qWaitFor([&] { return !app->covers()->localFile(list[0].coverUrl(400)).isEmpty(); }, 5000));
        QApplication::processEvents();
        const QImage shot = np->grab().toImage();
        const QRect c = np->coverRect();
        QCOMPARE(QColor(shot.pixel(c.center())), QColor(Qt::red));
        if (!qEnvironmentVariableIsEmpty("QIYAA_TEST_SHOTS"))
            app->snapshot().save(qEnvironmentVariable("QIYAA_TEST_SHOTS") + "/nowplaying.png");
        // Docked to the right of the main window by default.
        QCOMPARE(np->pos(), main->pos() + QPoint(main->width(), 0));
    }

    void snapshotContainsAllWindows() {
        const QImage img = app->snapshot();
        QCOMPARE(img.width(), 275);
        QCOMPARE(img.height(), 116 + 116 + 232);
    }
};

QTEST_MAIN(TestWindows)
#include "test_windows.moc"
