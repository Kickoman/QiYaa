#include "app/application.h"
#include "core/cover_cache.h"
#include "core/jam_mode.h"
#include "support/mock_http_server.h"
#include "ui/equalizer_window.h"
#include "ui/jam_window.h"
#include "ui/login_dialog.h"
#include "ui/main_window.h"
#include "ui/milkdrop_window.h"
#include "ui/now_playing_window.h"
#include "ui/playlist_window.h"
#include "yandex/api_client.h"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QList>
#include <QMenu>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QPoint>
#include <QPointF>
#include <QScreen>
#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>

#include <memory>

using Yandex::Track;

namespace {

void SendMouseEvent(
    QWidget* widget,
    QEvent::Type type,
    QPoint local,
    QPoint global,
    Qt::MouseButton button,
    Qt::MouseButtons buttons
) {
    QMouseEvent mouseEvent(type, QPointF(local), QPointF(global), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &mouseEvent);
}

void Drag(QWidget* widget, QPoint local, QPoint delta) {
    const QPoint global = widget->mapToGlobal(local);
    SendMouseEvent(widget, QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton);
    // Local coordinates are relative to the window's position at press time,
    // which is what Qt reports while the window follows the cursor.
    SendMouseEvent(
        widget, QEvent::MouseMove, local + delta, global + delta, Qt::NoButton, Qt::LeftButton
    );
    SendMouseEvent(
        widget, QEvent::MouseButtonRelease, local + delta, global + delta, Qt::LeftButton,
        Qt::NoButton
    );
}

void Click(QWidget* widget, QPoint local) {
    const QPoint global = widget->mapToGlobal(local);
    SendMouseEvent(widget, QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton);
    SendMouseEvent(widget, QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton);
}

QList<Yandex::Track> MakeTracks(int count) {
    QList<Yandex::Track> tracks;
    for (int i = 0; i < count; ++i) {
        Yandex::Track track;
        track.id = QString::number(i + 1);
        track.title = QStringLiteral("Track %1").arg(i + 1);
        track.artists << QStringLiteral("Artist");
        track.durationMs = 60'000 + i * 1000;
        tracks << track;
    }
    return tracks;
}

}  // namespace

class TestWindows : public QObject {
    Q_OBJECT
private:
    std::unique_ptr<App::Application> application;
    Ui::MainWindow* main = nullptr;
    Ui::EqualizerWindow* eq = nullptr;
    Ui::PlaylistWindow* playlist = nullptr;

    void requireBigScreen() {
        if (QGuiApplication::primaryScreen()->availableGeometry().height() < 1200) {
            QSKIP("needs a 2560x1440 virtual screen");
        }
    }

private Q_SLOTS:
    void init() {
        App::Application::Options options;
        options.offline = true;
        options.audio = false;
        options.readOnlySettings = true;
        options.mediaIntegration = false;
        options.language = App::Language::English;
        application = std::make_unique<App::Application>(options);
        application->start();
        main = application->mainWindow();
        eq = application->equalizerWindow();
        playlist = application->playlistWindow();
        QVERIFY(QTest::qWaitForWindowExposed(main));
    }
    void cleanup() { application.reset(); }

    void shuffleMenuChoosesAndPersistsThePreferredAlgorithm() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        App::Application::Options options;
        options.offline = true;
        options.audio = false;
        options.mediaIntegration = false;
        options.language = App::Language::English;
        options.settingsFile = directory.filePath(QStringLiteral("settings.ini"));
        {
            App::Application configured(options);
            QCOMPARE(
                configured.player()->shuffleAlgorithm(), Core::ShuffleAlgorithm::WithoutRepeats
            );
            configured.mainWindow()->menuRequested(QPoint(100, 100));
            auto* menu = configured.mainWindow()->findChild<QMenu*>(QStringLiteral("shuffleMenu"));
            QVERIFY(menu);
            QCOMPARE(menu->actions().size(), 2);
            QCOMPARE(menu->actions()[0]->text(), QStringLiteral("Random track"));
            QVERIFY(menu->actions()[1]->isChecked());
            menu->actions()[0]->trigger();
            QCOMPARE(configured.player()->shuffleAlgorithm(), Core::ShuffleAlgorithm::Random);
            QVERIFY(menu->actions()[0]->isChecked());
            QVERIFY(!menu->actions()[1]->isChecked());
            QVERIFY(!configured.player()->shuffle());
            configured.player()->setShuffle(true);
            QCOMPARE(configured.player()->shuffleAlgorithm(), Core::ShuffleAlgorithm::Random);
        }
        {
            App::Application restored(options);
            QCOMPARE(restored.player()->shuffleAlgorithm(), Core::ShuffleAlgorithm::Random);
            QVERIFY(!restored.player()->shuffle());
            restored.mainWindow()->menuRequested(QPoint(100, 100));
            auto* menu = restored.mainWindow()->findChild<QMenu*>(QStringLiteral("shuffleMenu"));
            QVERIFY(menu);
            QVERIFY(menu->actions()[0]->isChecked());
            menu->actions()[1]->trigger();
            QCOMPARE(restored.player()->shuffleAlgorithm(), Core::ShuffleAlgorithm::WithoutRepeats);
        }
        {
            App::Application restored(options);
            QCOMPARE(restored.player()->shuffleAlgorithm(), Core::ShuffleAlgorithm::WithoutRepeats);
        }
        QSettings saved(options.settingsFile, QSettings::IniFormat);
        saved.setValue(QStringLiteral("shuffle/algorithm"), QStringLiteral("unknown"));
        saved.sync();
        App::Application fallback(options);
        QCOMPARE(fallback.player()->shuffleAlgorithm(), Core::ShuffleAlgorithm::WithoutRepeats);
    }

    void defaultLayoutIsStacked() {
        QVERIFY(eq->isVisible() && playlist->isVisible());
        QCOMPARE(eq->pos(), main->pos() + QPoint(0, main->height()));
        QCOMPARE(playlist->pos(), eq->pos() + QPoint(0, eq->height()));
        QCOMPARE(main->dockedWindows().size(), 2);
    }

    void mainDragsDockedWindows() {
        const QPoint eqOffset = eq->pos() - main->pos();
        const QPoint playlistOffset = playlist->pos() - main->pos();
        Drag(main, {100, 5}, {40, 20});
        QCOMPARE(eq->pos() - main->pos(), eqOffset);
        QCOMPARE(playlist->pos() - main->pos(), playlistOffset);
    }

    void equalizerDetachesAndSnapsBack() {
        const QPoint start = eq->pos();
        Drag(eq, {100, 5}, {300, 0});
        QCOMPARE(eq->pos(), start + QPoint(300, 0));
        QVERIFY(!main->dockedWindows().contains(eq));
        Drag(eq, {100, 5}, {-292, 0});  // within 15 px of the old spot: snaps
        QCOMPARE(eq->pos(), start);
    }

    void scalingKeepsTheStack() {
        requireBigScreen();
        application->setScale(1.5);
        QCOMPARE(main->size(), QSize(413, 174));
        QCOMPARE(eq->pos(), main->pos() + QPoint(0, main->height()));
        QCOMPARE(playlist->pos(), eq->pos() + QPoint(0, eq->height()));
    }

    void scalingRoundTripsStayDocked() {
        requireBigScreen();
        for (double scaleFactor : {1.25, 1.75, 1.35, 1.0, 2.5, 1.5}) {
            application->setScale(scaleFactor);
            QCOMPARE(eq->pos(), main->pos() + QPoint(0, main->height()));
            QCOMPARE(playlist->pos(), eq->pos() + QPoint(0, eq->height()));
            QCOMPARE(main->dockedWindows().size(), 2);
        }
    }

    void hugeScaleKeepsEveryWindowReachable() {
        // Stack at 400% is 1856 px tall: taller than the 1440 px test screen.
        application->setScale(4.0);
        const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        for (QWidget* widget :
             {static_cast<QWidget*>(main), static_cast<QWidget*>(eq),
              static_cast<QWidget*>(playlist)}) {
            const QRect frame = widget->frameGeometry();
            QVERIFY2(screen.contains(frame.topLeft()), qPrintable(widget->windowTitle()));
            if (frame.width() <= screen.width() && frame.height() <= screen.height()) {
                QVERIFY(screen.contains(frame));
            }
        }
    }

    void hiddenWindowFollowsScale() {
        requireBigScreen();
        application->setEqualizerVisible(false);
        application->setScale(2.0);
        application->setEqualizerVisible(true);
        QCOMPARE(eq->pos(), main->pos() + QPoint(0, main->height()));
    }

    void newQueueClearsSelection() {
        application->player()->setQueue(MakeTracks(10), "A", false);
        Click(playlist, {60, 20 + 3 + 6});
        QCOMPARE(playlist->selection().size(), 1);
        application->player()->setQueue(MakeTracks(10), "B", false);
        QVERIFY(playlist->selection().isEmpty());
        QKeyEvent deleteKey(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QCoreApplication::sendEvent(playlist, &deleteKey);
        QCOMPARE(application->player()->playlist().size(), 10);
    }

    void shiftArrowsGrowTheRange() {
        application->player()->setQueue(MakeTracks(10), "A", false);
        Click(playlist, {60, 20 + 3 + 6});  // row 0
        for (int i = 0; i < 3; ++i) {
            QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::ShiftModifier);
            QCoreApplication::sendEvent(playlist, &down);
        }
        QCOMPARE(playlist->selection(), (QSet<int>{0, 1, 2, 3}));
    }

    void smoothScrollWheelChangesVolume() {
        main->setVolume(50);
        for (int i = 0; i < 3; ++i) {  // three 40-unit touchpad deltas = one notch
            QWheelEvent wheel(
                QPointF(150, 60), main->mapToGlobal(QPointF(150, 60)), QPoint(), QPoint(0, 40),
                Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false
            );
            QCoreApplication::sendEvent(main, &wheel);
        }
        QCOMPARE(main->volume(), 54);
    }

    void eqButtonTogglesWindow() {
        Click(main, {219 + 10, 58 + 5});
        QVERIFY(!eq->isVisible());
        Click(main, {219 + 10, 58 + 5});
        QVERIFY(eq->isVisible());
    }

    void playlistShowsQueueAndPlaysOnDoubleClick() {
        application->player()->setQueue(MakeTracks(50), "Test", false);
        QCOMPARE(playlist->visibleRows(), 13);  // default height 232: (232-58)/13
        const QPoint thirdRow(60, 20 + 3 + 2 * 13 + 6);
        QCOMPARE(playlist->rowAt(thirdRow), 2);
        // Double click plays that row (no audio device here, but the index moves).
        QMouseEvent doubleClick(
            QEvent::MouseButtonDblClick, QPointF(thirdRow),
            playlist->mapToGlobal(QPointF(thirdRow)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier
        );
        QCoreApplication::sendEvent(playlist, &doubleClick);
        QCOMPARE(application->player()->currentIndex(), 2);
    }

    void playlistScrollsAndResizes() {
        application->player()->setQueue(MakeTracks(50), "Test", false);
        QWheelEvent wheel(
            QPointF(60, 60), playlist->mapToGlobal(QPointF(60, 60)), QPoint(), QPoint(0, -120),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false
        );
        QCoreApplication::sendEvent(playlist, &wheel);
        QCOMPARE(playlist->scrollOffset(), 3);
        // Drag the resize grip by one step to the right and two down.
        const QPoint grip(playlist->width() - 5, playlist->height() - 5);
        Drag(playlist, grip, {25, 58});
        QCOMPARE(playlist->sizeSteps(), QSize(1, 6));
        QCOMPARE(playlist->size(), QSize(300, 290));
    }

    void deleteRemovesTheSelectedRows() {
        application->player()->setQueue(MakeTracks(5), "Test", false);
        Click(playlist, {60, 20 + 3 + 6});  // row 0
        QKeyEvent shiftDown(QEvent::KeyPress, Qt::Key_Down, Qt::ShiftModifier);
        QCoreApplication::sendEvent(playlist, &shiftDown);
        QCOMPARE(playlist->selection().size(), 2);
        QKeyEvent deleteKey(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QCoreApplication::sendEvent(playlist, &deleteKey);
        QCOMPARE(application->player()->playlist().size(), 3);
        QCOMPARE(application->player()->playlist().first().title, QStringLiteral("Track 3"));
    }

    void eqSliderSetsItsBandAndOnButtonDisables() {
        QSignalSpy changed(eq, &Ui::EqualizerWindow::settingsChanged);
        // Band 60 Hz at x=78, slider top y=38, 51 px travel: top = +12 dB.
        const QPoint top(78 + 7, 38 + 5);
        SendMouseEvent(
            eq, QEvent::MouseButtonPress, top, eq->mapToGlobal(top), Qt::LeftButton, Qt::LeftButton
        );
        SendMouseEvent(
            eq, QEvent::MouseButtonRelease, top, eq->mapToGlobal(top), Qt::LeftButton, Qt::NoButton
        );
        QVERIFY(changed.count() >= 1);
        QCOMPARE(eq->settings().bandsDb[0], 12.0);
        // ON button turns the EQ off.
        Click(eq, {14 + 5, 18 + 5});
        QVERIFY(!eq->settings().enabled);
    }

    void visualizerAndTimeModesToggle() {
        const auto before = main->visMode();
        Click(main, {24 + 10, 43 + 5});
        QVERIFY(main->visMode() != before);
        const bool remaining = main->showsRemainingTime();
        Click(main, {39 + 20, 26 + 5});
        QCOMPARE(main->showsRemainingTime(), !remaining);
    }

    void loginDialogFitsItsText() {
        QNetworkAccessManager networkManager;
        // Unreachable OAuth server: the device flow fails fast with a long message.
        Ui::LoginDialog dialog(&networkManager, nullptr, QStringLiteral("http://127.0.0.1:1"));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto allTextFits = [&dialog] {
            for (QLabel* label : dialog.findChildren<QLabel*>()) {
                if (!label->isVisible() || label->text().isEmpty()) {
                    continue;
                }
                const int neededHeight = label->wordWrap() ? label->heightForWidth(label->width())
                                                           : label->sizeHint().height();
                if (label->height() < neededHeight) {
                    qWarning(
                        "label %s: %d < %d", qPrintable(label->text().left(30)), label->height(),
                        neededHeight
                    );
                    return false;
                }
            }
            return true;
        };
        // What the window manager did on GNOME: squeeze it to a small height.
        dialog.resize(dialog.width(), 150);
        QApplication::processEvents();
        QVERIFY(allTextFits());
        QVERIFY(QTest::qWaitFor(
            [&] {
                for (QLabel* label : dialog.findChildren<QLabel*>()) {
                    if (label->text().contains(QStringLiteral("failed"))) {
                        return true;
                    }
                }
                return false;
            },
            5000
        ));
        QApplication::processEvents();
        QVERIFY(allTextFits());
        dialog.resize(dialog.width(), 150);
        QApplication::processEvents();
        QVERIFY(allTextFits());
    }

    void shadeModesKeepTheStack() {
        application->player()->setQueue(MakeTracks(3), "A", false);
        const QPoint mainPosition = main->pos();
        main->setShaded(true);
        QCOMPARE(main->size(), QSize(275, 14));
        QCOMPARE(eq->pos(), mainPosition + QPoint(0, 14));
        QCOMPARE(playlist->pos(), eq->pos() + QPoint(0, eq->height()));
        eq->setShaded(true);
        QCOMPARE(eq->height(), 14);
        QCOMPARE(playlist->pos(), mainPosition + QPoint(0, 28));
        playlist->setShaded(true);
        QCOMPARE(playlist->size(), QSize(275, 14));
        if (!qEnvironmentVariableIsEmpty("QIYAA_TEST_SHOTS")) {
            application->snapshot().save(qEnvironmentVariable("QIYAA_TEST_SHOTS") + "/shaded.png");
        }
        main->setShaded(false);
        eq->setShaded(false);
        playlist->setShaded(false);
        QCOMPARE(eq->pos(), mainPosition + QPoint(0, 116));
        QCOMPARE(playlist->pos(), mainPosition + QPoint(0, 232));
        QCOMPARE(playlist->size(), QSize(275, 232));
    }

    void shadeLeavesWindowsOthersHold() {
        // Main and EQ side by side, playlist under the EQ.
        const QPoint mainPosition = main->pos();
        eq->move(mainPosition + QPoint(275, 0));
        playlist->move(mainPosition + QPoint(275, 116));
        main->setShaded(true);
        QCOMPARE(eq->pos(), mainPosition + QPoint(275, 0));
        QCOMPARE(playlist->pos(), mainPosition + QPoint(275, 116));
        main->setShaded(false);
        QCOMPARE(playlist->pos(), mainPosition + QPoint(275, 116));
    }

    void hiddenWindowFollowsShade() {
        application->setPlaylistVisible(false);
        main->setShaded(true);
        application->setPlaylistVisible(true);
        QCOMPARE(eq->pos(), main->pos() + QPoint(0, 14));
        QCOMPARE(playlist->pos(), eq->pos() + QPoint(0, eq->height()));
    }

    void positionsAreFinalWhenShadeChanges() {
        // The app saves positions on shadeChanged.
        QPoint eqAtSignal;
        connect(main, &Ui::SkinnedWindow::shadeChanged, this, [&] { eqAtSignal = eq->pos(); });
        main->setShaded(true);
        QCOMPARE(eqAtSignal, main->pos() + QPoint(0, 14));
    }

    void playlistScrollIsValidAfterUnshade() {
        application->player()->setQueue(MakeTracks(40), "A", false);
        playlist->setShaded(true);
        application->player()->playIndex(39);  // scrolls the one-row shaded view to row 39
        playlist->setShaded(false);
        QCOMPARE(
            playlist->scrollOffset(), 40 - 13
        );  // 13 rows fit the default height; last row at the bottom
    }

    void unshadingAtTheBottomStaysOnScreen() {
        main->setShaded(true);
        eq->setShaded(true);
        playlist->setShaded(true);
        const QRect screen = main->screen()->availableGeometry();
        const int y =
            screen.y() + screen.height() - 42;  // the shaded stack sits on the bottom edge
        main->move(main->x(), y);
        eq->move(main->x(), y + 14);
        playlist->move(main->x(), y + 28);
        main->setShaded(false);
        QVERIFY(screen.contains(playlist->frameGeometry()));
        QCOMPARE(eq->pos(), main->pos() + QPoint(0, 116));
        QCOMPARE(playlist->pos(), eq->pos() + QPoint(0, 14));
    }

    void nothingPlaysAfterShutDown() {
        application->player()->setQueue(MakeTracks(3), "A", false);
        application->player()->shutDown();
        application->player()->playIndex(2);
        QCOMPARE(application->player()->currentIndex(), 0);
    }

    void milkdropWindowOpensBesideTheEqualizer() {
        Ui::MilkdropWindow* milkdrop = application->milkdropWindow();
#if defined(QIYAA_HAVE_MILKDROP)
        QVERIFY(milkdrop);
        QVERIFY(!milkdrop->isVisible());
        application->setMilkdropVisible(true);
        QVERIFY(milkdrop->isVisible());
        QCOMPARE(
            milkdrop->pos(), main->pos() + QPoint(main->width(), main->height())
        );  // right of the equalizer
        QVERIFY(milkdrop->presets().size() >= 50);
        if (!qEnvironmentVariableIsEmpty("QIYAA_TEST_SHOTS")) {
            application->snapshot().save(
                qEnvironmentVariable("QIYAA_TEST_SHOTS") + "/milkdrop-window.png"
            );
        }
        application->setMilkdropVisible(false);
        QVERIFY(!milkdrop->isVisible());
#else
        QVERIFY(!milkdrop);
        application->setMilkdropVisible(true);  // no-op
#endif
    }

    void doubleClickTitleShades() {
        QMouseEvent doubleClick(
            QEvent::MouseButtonDblClick, QPointF(100, 5), main->mapToGlobal(QPointF(100, 5)),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier
        );
        QCoreApplication::sendEvent(main, &doubleClick);
        QVERIFY(main->isShaded());
        QCoreApplication::sendEvent(main, &doubleClick);
        QVERIFY(!main->isShaded());
    }

    void eqShadeSlidersDriveMainVolume() {
        eq->setShaded(true);
        const QPoint volumeRight(61 + 96, 7);  // right end of the mini volume slider
        SendMouseEvent(
            eq, QEvent::MouseButtonPress, volumeRight, eq->mapToGlobal(volumeRight), Qt::LeftButton,
            Qt::LeftButton
        );
        SendMouseEvent(
            eq, QEvent::MouseButtonRelease, volumeRight, eq->mapToGlobal(volumeRight),
            Qt::LeftButton, Qt::NoButton
        );
        QCOMPARE(main->volume(), 100);
        const QPoint balanceLeft(164, 7);  // left end of the mini balance slider
        SendMouseEvent(
            eq, QEvent::MouseButtonPress, balanceLeft, eq->mapToGlobal(balanceLeft), Qt::LeftButton,
            Qt::LeftButton
        );
        SendMouseEvent(
            eq, QEvent::MouseButtonRelease, balanceLeft, eq->mapToGlobal(balanceLeft),
            Qt::LeftButton, Qt::NoButton
        );
        QCOMPARE(main->balance(), -100);
    }

    void mainShadeTransportWorks() {
        application->player()->setQueue(MakeTracks(3), "A", false);
        main->setShaded(true);
        Click(main, {204 + 4, 6});  // mini "next"
        QCOMPARE(application->player()->currentIndex(), 1);
        Click(main, {169 + 3, 6});  // mini "previous"
        QCOMPARE(application->player()->currentIndex(), 0);
    }

    void nowPlayingShowsCoverAndDetails() {
        Tests::MockHttpServer server;
        QImage redCover(64, 64, QImage::Format_RGB32);
        redCover.fill(Qt::red);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        redCover.save(&buffer, "PNG");
        server.on("GET", "/cover/400x400", [png](const Tests::MockRequest&) {
            return Tests::MockResponse{200, png};
        });

        QList<Yandex::Track> tracks = MakeTracks(1);
        tracks[0].title = QStringLiteral("Группа крови");
        tracks[0].artists = {QStringLiteral("Кино")};
        tracks[0].albumTitle = QStringLiteral("Группа крови");
        tracks[0].year = 1988;
        tracks[0].coverUri = server.baseUrl() + QStringLiteral("/cover/%%");
        application->player()->setQueue(tracks, "A", false);
        application->setNowPlayingVisible(true);
        Ui::NowPlayingWindow* nowPlaying = application->nowPlayingWindow();
        QVERIFY(QTest::qWaitForWindowExposed(nowPlaying));
        QVERIFY(QTest::qWaitFor(
            [&] { return !application->covers()->localFile(tracks[0].coverUrl(400)).isEmpty(); },
            5000
        ));
        QApplication::processEvents();
        const QImage shot = nowPlaying->grab().toImage();
        const QRect coverArea = nowPlaying->coverRect();
        QCOMPARE(QColor(shot.pixel(coverArea.center())), QColor(Qt::red));
        if (!qEnvironmentVariableIsEmpty("QIYAA_TEST_SHOTS")) {
            application->snapshot().save(
                qEnvironmentVariable("QIYAA_TEST_SHOTS") + "/nowplaying.png"
            );
        }
        QCOMPARE(nowPlaying->pos(), main->pos() + QPoint(main->width(), 0));
    }

    void snapshotContainsAllWindows() {
        const QImage image = application->snapshot();
        QCOMPARE(image.width(), 275);
        QCOMPARE(image.height(), 116 + 116 + 232);
    }

    void theJamWindowIsOneOfTheWindows() {
#ifdef QIYAA_HAVE_JAM
        Ui::JamWindow* jam = application->jamWindow();
        QVERIFY(jam);
        QVERIFY(!jam->isVisible());
        application->setJamWindowVisible(true);
        QVERIFY(jam->isVisible());
        QCOMPARE(jam->skinSize(), QSize(300, 348));
        application->setScale(2.0);
        QCOMPARE(jam->scale(), 2.0);
        application->setScale(1.0);
        application->setJamWindowVisible(false);
        QVERIFY(!jam->isVisible());
#else
        QSKIP("built without the jam");
#endif
    }

    void duringAJamThePlaylistDoesNotEditTheQueue() {  // HOST-21
#ifdef QIYAA_HAVE_JAM
        application->player()->setQueue(MakeTracks(3), "A", false);
        application->player()->playIndex(0);
        application->jam()->start(QStringLiteral("Джем"), false);
        QVERIFY(application->jam()->isActive());
        const qsizetype size = application->player()->playlist().size();
        Click(playlist, {60, 20 + 3 + 6});  // row 0
        QKeyEvent deleteKey(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QCoreApplication::sendEvent(playlist, &deleteKey);
        QCOMPARE(application->player()->playlist().size(), size);
        application->jam()->end();
        Click(playlist, {60, 20 + 3 + 6});
        QCoreApplication::sendEvent(playlist, &deleteKey);
        QCOMPARE(application->player()->playlist().size(), size - 1);
#else
        QSKIP("built without the jam");
#endif
    }
};

QTEST_MAIN(TestWindows)
#include "windows_test.moc"
