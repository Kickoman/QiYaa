#include "app/application.h"
#include "app/window_set.h"
#include "core/continuation.h"
#include "core/cover_cache.h"
#include "core/jam_mode.h"
#ifdef QIYAA_HAVE_JAM
#include "jam/host_session.h"
#include "jam/session_store.h"
#include "support/jam_stub_server.h"
#endif
#include "sources/yandex/tracks.h"
#include "support/mock_http_server.h"
#include "support/spec_fixtures.h"
#include "ui/equalizer_window.h"
#include "ui/jam_window.h"
#include "ui/login_dialog.h"
#include "ui/main_window.h"
#include "ui/milkdrop_window.h"
#include "ui/now_playing_window.h"
#include "ui/playlist_window.h"
#include "yandex/api_client.h"
#include "yandex/file_info.h"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QList>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>

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
    SendMouseEvent(widget, QEvent::MouseMove, local + delta, global + delta, Qt::NoButton, Qt::LeftButton);
    SendMouseEvent(widget, QEvent::MouseButtonRelease, local + delta, global + delta, Qt::LeftButton, Qt::NoButton);
}

void Click(QWidget* widget, QPoint local) {
    const QPoint global = widget->mapToGlobal(local);
    SendMouseEvent(widget, QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton);
    SendMouseEvent(widget, QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton);
}

QMenu* OpenContextMenu(QWidget* widget, QPoint local, QContextMenuEvent::Reason reason = QContextMenuEvent::Mouse) {
    QContextMenuEvent event(reason, local, widget->mapToGlobal(local));
    QCoreApplication::sendEvent(widget, &event);
    return qobject_cast<QMenu*>(QApplication::activePopupWidget());
}

void PressReturn(QMenu* menu, QAction* item) {
    menu->setActiveAction(item);
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(menu, &press);
}

QAction* OpenMenuItem(const QString& textOrName) {
    const QWidget* menu = QApplication::activePopupWidget();
    if (!menu) {
        return nullptr;
    }
    const QList<QAction*> items = menu->findChildren<QAction*>();
    const auto found = std::find_if(items.begin(), items.end(), [&textOrName](const QAction* item) {
        return item->text() == textOrName || item->objectName() == textOrName;
    });
    return found == items.end() ? nullptr : *found;
}

QString DialogAfterChoosing(QAction* item) {
    if (!item) {
        return QStringLiteral("no such item");
    }
    QList<QMenu*> path;
    for (auto* menu = qobject_cast<QMenu*>(item->parent()); menu; menu = qobject_cast<QMenu*>(menu->parent())) {
        path.prepend(menu);
    }
    for (qsizetype i = 1; i < path.size(); ++i) {
        PressReturn(path[i - 1], path[i]->menuAction());
        if (!QTest::qWaitFor([&] { return path[i]->isVisible(); }, 1000)) {
            return QStringLiteral("no submenu ") + path[i]->title();
        }
    }
    const QPointer<QMenu> top = path.first();
    std::optional<QString> during;
    std::optional<QString> after;
    bool choosing = true;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, [&] {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            const auto* box = qobject_cast<QMessageBox*>(dialog);
            (choosing ? during : after) = box ? box->text() : dialog->windowTitle();
            dialog->reject();
        }
    });
    poll.start(5);
    PressReturn(path.last(), item);
    choosing = false;
    if (!QTest::qWaitFor([&] { return during || (after && !top); }, 5000)) {
        return top ? QStringLiteral("menu still open") : QStringLiteral("no dialog");
    }
    return during ? QStringLiteral("inside the menu: ") + *during : *after;
}

class NoMoreTracks : public Core::Continuation {
public:
    bool more(const Core::ContinuationRequest&, TDone) override { return false; }
    void trackEvent(Core::TrackEvent, const Core::Track&, double) override { }
};

QAction* FindAction(QMenu* menu, const QString& text) {
    for (QAction* action : menu->actions()) {
        if (action->text() == text) {
            return action;
        }
    }
    return nullptr;
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

App::Application::Options OptionsWithSettings(const QString& settingsFile) {
    App::Application::Options options;
    options.offline = true;
    options.audio = false;
    options.mediaIntegration = false;
    options.language = App::Language::English;
    options.settingsFile = settingsFile;
    return options;
}

QVariantMap AllSettings(const QString& settingsFile) {
    const QSettings settings(settingsFile, QSettings::IniFormat);
    QVariantMap values;
    for (const QString& key : settings.allKeys()) {
        values.insert(key, settings.value(key));
    }
    return values;
}

}  // namespace

class TestWindows : public QObject {
    Q_OBJECT
private:
    std::unique_ptr<App::Application> application;
    Ui::MainWindow* main = nullptr;
    Ui::EqualizerWindow* eq = nullptr;
    Ui::PlaylistWindow* playlist = nullptr;
    QUrl openedUrl;

    bool connectLibrary(Tests::MockHttpServer& server) {
        application->api()->setBaseUrl(server.baseUrl());
        application->api()->setToken(QStringLiteral("test-token"));
        server.fixture("GET", "/account/status", "account-status/ok");
        server.fixture("POST", "/users/42/likes/tracks/add-multiple", "users-likes-tracks-add-multiple/ok");
        server.fixture("POST", "/users/42/likes/tracks/remove", "users-likes-tracks-remove/ok");
        server.fixture("POST", "/users/42/dislikes/tracks/add-multiple", "users-dislikes-tracks-add-multiple/ok");
        bool done = false;
        application->library()->connectAccount([&](const Yandex::Account&, const QString&) { done = true; });
        return QTest::qWaitFor([&] { return done; }, 5000) && application->library()->isLoggedIn();
    }

#ifdef QIYAA_HAVE_JAM
    // HOST-23: an app that starts logged in (QIYAA_TOKEN, the catalog on a MockHttpServer) with a
    // jam stored from the last run, on a stub jam server.
    struct StoredJamStart {
        QTemporaryDir directory;
        Tests::JamStubServer jamServer;
        Tests::MockHttpServer catalog;
        int likesAsked = 0;
        std::unique_ptr<App::Application> application;
        QPointer<QMessageBox> question;

        StoredJamStart() {
            const QString settingsFile = directory.filePath(QStringLiteral("settings.ini"));
            QSettings settings(settingsFile, QSettings::IniFormat);
            settings.setValue(QStringLiteral("jam/server"), jamServer.serverUrl());
            settings.sync();
            const Jam::SessionStore store(directory.filePath(QStringLiteral("jam-session.json")));
            store.save(Jam::Session{
                QStringLiteral("7k3m9q2x"),
                QStringLiteral("R0CuY0ewFywBJU_1W65a_1GZ9ERuf21kPUAYWz9HUUU"),
                QStringLiteral("https://jam.example.org/j/7k3m9q2x#WDkyFgMr5iV3hKwManPvsg"),
                std::nullopt,
                {}
            });
            catalog.fixture("GET", "/account/status", "account-status/ok");
            catalog.fixture("POST", "/tracks/", "tracks/two-tracks");
            catalog.on("GET", "/users/42/likes/tracks", [this](const Tests::MockRequest&) {
                ++likesAsked;
                return Tests::MockResponse{200, Tests::Fixture(QStringLiteral("users-likes-tracks/string-ids"))};
            });
            qputenv("QIYAA_TOKEN", "test-token");
            App::Application::Options options = OptionsWithSettings(settingsFile);
            options.offline = false;
            application = std::make_unique<App::Application>(options);
            application->api()->setBaseUrl(catalog.baseUrl());
            application->start();
            question = application->mainWindow()->findChild<QMessageBox*>();
        }

        bool loggedIn() const {
            return QTest::qWaitFor([this] { return application->library()->isLoggedIn(); }, 5000);
        }

        void answer(const QString& text) const {
            const QList<QAbstractButton*> buttons = question->buttons();
            const auto button = std::find_if(buttons.cbegin(), buttons.cend(), [&text](QAbstractButton* candidate) {
                return candidate->text() == text;
            });
            if (button != buttons.cend()) {
                (*button)->click();
            }
        }

        bool resumed() {
            if (!QTest::qWaitFor([this] { return jamServer.count() == 1 && !jamServer.received().isEmpty(); }, 5000)) {
                return false;
            }
            jamServer.welcome();
            return QTest::qWaitFor(
                [this] {
                    const std::vector<Jam::ClientMessage> messages = jamServer.messages();
                    return std::any_of(messages.cbegin(), messages.cend(), [](const Jam::ClientMessage& message) {
                        return std::holds_alternative<Jam::Resume>(message);
                    });
                },
                5000
            );
        }
    };
#endif

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
    void cleanup() {
        qunsetenv("QIYAA_TOKEN");
        QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
        openedUrl.clear();
        application.reset();
    }

    void recordTrackUrl(const QUrl& url) { openedUrl = url; }

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
            QCOMPARE(configured.player()->shuffleAlgorithm(), Core::ShuffleAlgorithm::WithoutRepeats);
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

    void qualityMenuChoosesAndPersistsTheQuality() {  // spec QUAL-06
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
            QCOMPARE(configured.yandex()->quality(), Yandex::Quality::Standard);
            configured.mainWindow()->menuRequested(QPoint(100, 100));
            auto* menu = configured.mainWindow()->findChild<QMenu*>(QStringLiteral("qualityMenu"));
            QVERIFY(menu);
            QCOMPARE(menu->actions().size(), 2);
            QCOMPARE(menu->actions()[0]->text(), QStringLiteral("Standard"));
            QVERIFY(menu->actions()[0]->isChecked());
            menu->actions()[1]->trigger();
            QCOMPARE(configured.yandex()->quality(), Yandex::Quality::Economy);
            QVERIFY(menu->actions()[1]->isChecked());
            QVERIFY(!menu->actions()[0]->isChecked());
        }
        {
            App::Application restored(options);
            QCOMPARE(restored.yandex()->quality(), Yandex::Quality::Economy);
            restored.mainWindow()->menuRequested(QPoint(100, 100));
            auto* menu = restored.mainWindow()->findChild<QMenu*>(QStringLiteral("qualityMenu"));
            QVERIFY(menu);
            QVERIFY(menu->actions()[1]->isChecked());
        }
        QSettings saved(options.settingsFile, QSettings::IniFormat);
        QCOMPARE(saved.value(QStringLiteral("playback/quality")).toString(), QStringLiteral("economy"));
        saved.setValue(QStringLiteral("playback/quality"), QStringLiteral("lossless"));
        saved.sync();
        App::Application fallback(options);
        QCOMPARE(fallback.yandex()->quality(), Yandex::Quality::Standard);
    }

    void theVibeItemSwitchesOnlyTheCurrentQueue() {  // CONT-01, CONT-02, CONT-12
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        App::Application::Options options;
        options.offline = true;
        options.audio = false;
        options.mediaIntegration = false;
        options.language = App::Language::English;
        options.settingsFile = directory.filePath(QStringLiteral("settings.ini"));
        {
            QSettings saved(options.settingsFile, QSettings::IniFormat);
            saved.setValue(QStringLiteral("playback/continueWithVibe"), false);
        }
        App::Application configured(options);
        Core::Player* player = configured.player();
        const auto openItem = [&configured] {
            configured.mainWindow()->menuRequested(QPoint(100, 100));
            return configured.mainWindow()->findChild<QAction*>(QStringLiteral("continueWithVibe"));
        };
        const Core::QueueSource plain{.title = QStringLiteral("Plain"), .head = YandexSource::TracksOf(MakeTracks(2))};
        player->setSource(plain, false);
        QAction* item = openItem();
        QVERIFY(item);
        QCOMPARE(item->text(), QStringLiteral("Continue with a vibe"));
        QVERIFY(item->isCheckable());
        QVERIFY(item->isEnabled());
        QVERIFY(!item->isChecked());
        item->trigger();
        QVERIFY(player->continuesWithVibe());
        delete item->parent();
        item = openItem();
        QVERIFY(item->isChecked());
        delete item->parent();
        player->setSource(plain, false);
        QVERIFY(!player->continuesWithVibe());
        player->setSource(
            Core::QueueSource{
                .title = QStringLiteral("Vibe"),
                .continuation = std::make_shared<NoMoreTracks>(),
                .given = YandexSource::TracksOf(MakeTracks(1))
            },
            false
        );
        item = openItem();
        QVERIFY(!item->isEnabled());
        delete item->parent();
        player->setContinueWithVibeByDefault(true);
        player->setSource(
            Core::QueueSource{
                .title = QStringLiteral("Local"),
                .head = YandexSource::TracksOf(MakeTracks(2)),
                .waveFromHistory = false
            },
            false
        );
        item = openItem();
        QVERIFY(!item->isEnabled());
        QVERIFY(!item->isChecked());
        delete item->parent();
        QCOMPARE(
            QSettings(options.settingsFile, QSettings::IniFormat).value(QStringLiteral("playback/continueWithVibe")),
            QVariant(false)
        );
    }

    void bothMenusOpenLocalFilesWithoutAnAccount() {  // LOCAL-01, LOCAL-02
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        App::Application::Options options;
        options.offline = true;
        options.audio = false;
        options.mediaIntegration = false;
        options.language = App::Language::English;
        options.settingsFile = directory.filePath(QStringLiteral("settings.ini"));
        App::Application configured(options);
        QVERIFY(!configured.library()->isLoggedIn());
        const auto check = [&configured](const std::function<void()>& open) {
            open();
            QAction* files = configured.mainWindow()->findChild<QAction*>(QStringLiteral("openLocalFiles"));
            QAction* folder = configured.mainWindow()->findChild<QAction*>(QStringLiteral("openLocalFolder"));
            QVERIFY(files && folder);
            QCOMPARE(files->text(), QStringLiteral("Open files…"));
            QCOMPARE(folder->text(), QStringLiteral("Open folder…"));
            QVERIFY(files->isEnabled() && folder->isEnabled());
            QCOMPARE(files->parent(), folder->parent());
            const auto* menu = qobject_cast<QMenu*>(files->parent());
            QVERIFY(menu);
            QCOMPARE(menu->actions().first(), files);
            delete menu;
        };
        check([&configured] { configured.mainWindow()->menuRequested(QPoint(100, 100)); });
        check([&configured] { Q_EMIT configured.mainWindow()->sourcesMenuRequested(QPoint(100, 100)); });
    }

    void menuItemsOpenTheirDialogsOnlyAfterTheMenuIsGone() {
        const auto mainItem = [this](const QString& name) {
            Q_EMIT main->menuRequested(QPoint(100, 100));
            return OpenMenuItem(name);
        };
        QCOMPARE(DialogAfterChoosing(mainItem(QStringLiteral("openLocalFiles"))), QStringLiteral("Open files"));
        QCOMPARE(DialogAfterChoosing(mainItem(QStringLiteral("openLocalFolder"))), QStringLiteral("Open folder"));
        QCOMPARE(DialogAfterChoosing(mainItem(QStringLiteral("loadSkinFile"))), QStringLiteral("Winamp skin"));
#ifdef QIYAA_HAVE_JAM
        QCOMPARE(DialogAfterChoosing(mainItem(QStringLiteral("jamServerSettings"))), QStringLiteral("Jam server"));
#endif
        const auto presetsItem = [this](const QString& text) {
            Click(eq, {217 + 5, 18 + 5});
            return OpenMenuItem(text);
        };
        QCOMPARE(DialogAfterChoosing(presetsItem(QStringLiteral("Load .eqf…"))), QStringLiteral("Equalizer preset"));
        QCOMPARE(DialogAfterChoosing(presetsItem(QStringLiteral("Save to .eqf…"))), QStringLiteral("Save the preset"));
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
        application->windows()->setScale(1.5);
        QCOMPARE(main->size(), QSize(413, 174));
        QCOMPARE(eq->pos(), main->pos() + QPoint(0, main->height()));
        QCOMPARE(playlist->pos(), eq->pos() + QPoint(0, eq->height()));
    }

    void scalingRoundTripsStayDocked() {
        requireBigScreen();
        for (double scaleFactor : {1.25, 1.75, 1.35, 1.0, 2.5, 1.5}) {
            application->windows()->setScale(scaleFactor);
            QCOMPARE(eq->pos(), main->pos() + QPoint(0, main->height()));
            QCOMPARE(playlist->pos(), eq->pos() + QPoint(0, eq->height()));
            QCOMPARE(main->dockedWindows().size(), 2);
        }
    }

    void hugeScaleKeepsEveryWindowReachable() {
        // Stack at 400% is 1856 px tall: taller than the 1440 px test screen.
        application->windows()->setScale(4.0);
        const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        for (QWidget* widget :
             {static_cast<QWidget*>(main), static_cast<QWidget*>(eq), static_cast<QWidget*>(playlist)}) {
            const QRect frame = widget->frameGeometry();
            QVERIFY2(screen.contains(frame.topLeft()), qPrintable(widget->windowTitle()));
            if (frame.width() <= screen.width() && frame.height() <= screen.height()) {
                QVERIFY(screen.contains(frame));
            }
        }
    }

    void hiddenWindowFollowsScale() {
        requireBigScreen();
        application->windows()->setVisible(application->equalizerWindow(), false);
        application->windows()->setScale(2.0);
        application->windows()->setVisible(application->equalizerWindow(), true);
        QCOMPARE(eq->pos(), main->pos() + QPoint(0, main->height()));
    }

    void newQueueClearsSelection() {
        application->player()->setSource(
            Core::QueueSource{.title = QStringLiteral("A"), .head = YandexSource::TracksOf(MakeTracks(10))}, false
        );
        Click(playlist, {60, 20 + 3 + 6});
        QCOMPARE(playlist->selection().size(), 1);
        application->player()->setSource(
            Core::QueueSource{.title = QStringLiteral("B"), .head = YandexSource::TracksOf(MakeTracks(10))}, false
        );
        QVERIFY(playlist->selection().isEmpty());
        QKeyEvent deleteKey(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QCoreApplication::sendEvent(playlist, &deleteKey);
        QCOMPARE(application->player()->entries().size(), 10);
    }

    void shiftArrowsGrowTheRange() {
        application->player()->setSource(
            Core::QueueSource{.title = QStringLiteral("A"), .head = YandexSource::TracksOf(MakeTracks(10))}, false
        );
        Click(playlist, {60, 20 + 3 + 6});  // row 0
        for (int i = 0; i < 3; ++i) {
            QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::ShiftModifier);
            QCoreApplication::sendEvent(playlist, &down);
        }
        QCOMPARE(playlist->selection(), (QSet<int>{0, 1, 2, 3}));
    }

    void playlistTrackActionsUseTheClickedTrack_data() {
        QTest::addColumn<QString>("actionText");
        QTest::addColumn<QString>("requestPath");
        QTest::addColumn<bool>("alreadySelected");
        for (bool selected : {false, true}) {
            const QByteArray suffix = selected ? "-selected" : "-unselected";
            QTest::newRow(("like" + suffix).constData()
            ) << QStringLiteral("Like")
              << QStringLiteral("/users/42/likes/tracks/add-multiple") << selected;
            QTest::newRow(("unlike" + suffix).constData()
            ) << QStringLiteral("Remove from Liked")
              << QStringLiteral("/users/42/likes/tracks/remove") << selected;
            QTest::newRow(("dislike" + suffix).constData()
            ) << QStringLiteral("Dislike")
              << QStringLiteral("/users/42/dislikes/tracks/add-multiple") << selected;
            QTest::newRow(("browser" + suffix).constData()) << QStringLiteral("Open the track in the browser"
            ) << QString() << selected;
        }
    }

    void playlistTrackActionsUseTheClickedTrack() {
        QFETCH(QString, actionText);
        QFETCH(QString, requestPath);
        QFETCH(bool, alreadySelected);
        Tests::MockHttpServer server;
        QVERIFY(connectLibrary(server));
        const QList<Track> tracks = MakeTracks(5);
        application->player()->setSource(
            Core::QueueSource{.title = QStringLiteral("Test"), .head = YandexSource::TracksOf(tracks)}, false
        );
        const QString likedId = actionText == QStringLiteral("Remove from Liked") ? tracks[2].id : tracks[0].id;
        application->library()->setLiked(likedId, true, [](bool, const QString&) {});
        QTRY_VERIFY(application->library()->isLiked(likedId));
        Click(playlist, {60, 20 + 3 + 13 + 6});
        if (alreadySelected) {
            for (int i = 0; i < 2; ++i) {
                QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::ShiftModifier);
                QCoreApplication::sendEvent(playlist, &down);
            }
        }
        QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "recordTrackUrl");
        QMenu* menu = OpenContextMenu(playlist, {60, 20 + 3 + 2 * 13 + 6});
        QVERIFY(menu);
        QCOMPARE(playlist->selection(), alreadySelected ? (QSet<int>{1, 2, 3}) : (QSet<int>{2}));
        QAction* action = FindAction(menu, actionText);
        QVERIFY(action);
        QVERIFY(action->isEnabled());
        const qsizetype before = server.requests().size();
        action->trigger();
        menu->close();
        if (requestPath.isEmpty()) {
            QCOMPARE(openedUrl, tracks[2].webUrl());
        } else {
            QTRY_VERIFY(server.requests().size() > before && server.last(requestPath));
            QCOMPARE(server.last(requestPath)->formValue("track-ids"), tracks[2].id);
        }
        QCOMPARE(application->player()->currentIndex(), 0);
    }

    void playlistMenuKeepsItsTrackWhenTheQueueChanges() {
        Tests::MockHttpServer server;
        QVERIFY(connectLibrary(server));
        const QList<Track> tracks = MakeTracks(3);
        application->player()->setSource(
            Core::QueueSource{.title = QStringLiteral("Test"), .head = YandexSource::TracksOf(tracks)}, false
        );
        QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "recordTrackUrl");
        QMenu* menu = OpenContextMenu(playlist, {60, 20 + 3 + 2 * 13 + 6});
        QVERIFY(menu);
        QAction* action = FindAction(menu, QStringLiteral("Open the track in the browser"));
        QVERIFY(action);
        QList<Track> replacement = MakeTracks(1);
        replacement[0].id = QStringLiteral("999");
        application->player()->setSource(
            Core::QueueSource{.title = QStringLiteral("Replacement"), .head = YandexSource::TracksOf(replacement)},
            false
        );
        action->trigger();
        menu->close();
        QCOMPARE(openedUrl, tracks[2].webUrl());
        QCOMPARE(application->player()->currentTrack()->key.id, replacement[0].id);
    }

    void playlistKeyboardMenuUsesTheFocusedRow() {
        Tests::MockHttpServer server;
        QVERIFY(connectLibrary(server));
        const QList<Track> tracks = MakeTracks(3);
        application->player()->setSource(
            Core::QueueSource{.title = QStringLiteral("Test"), .head = YandexSource::TracksOf(tracks)}, false
        );
        Click(playlist, {60, 20 + 3 + 2 * 13 + 6});
        QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "recordTrackUrl");
        QMenu* menu = OpenContextMenu(playlist, {0, 0}, QContextMenuEvent::Keyboard);
        QVERIFY(menu);
        QAction* action = FindAction(menu, QStringLiteral("Open the track in the browser"));
        QVERIFY(action);
        action->trigger();
        menu->close();
        QCOMPARE(openedUrl, tracks[2].webUrl());
        QCOMPARE(application->player()->currentIndex(), 0);
    }

    void playlistMenuTargetsTheRowAfterScrollingAndScaling() {
        Tests::MockHttpServer server;
        QVERIFY(connectLibrary(server));
        const QList<Track> tracks = MakeTracks(30);
        application->player()->setSource(
            Core::QueueSource{.title = QStringLiteral("Test"), .head = YandexSource::TracksOf(tracks)}, false
        );
        application->windows()->setScale(1.5);
        playlist->setScrollOffset(10);
        QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "recordTrackUrl");
        QMenu* menu = OpenContextMenu(playlist, {90, qRound((20 + 3 + 2 * 13 + 6) * 1.5)});
        QVERIFY(menu);
        QAction* action = FindAction(menu, QStringLiteral("Open the track in the browser"));
        QVERIFY(action);
        action->trigger();
        menu->close();
        QCOMPARE(openedUrl, tracks[12].webUrl());
        QCOMPARE(application->player()->currentIndex(), 0);
    }

    void emptyPlaylistDisablesTrackActions() {
        Tests::MockHttpServer server;
        QVERIFY(connectLibrary(server));
        application->player()->clear();
        QMenu* menu = OpenContextMenu(playlist, {60, 29});
        QVERIFY(menu);
        for (const QString& text :
             {QStringLiteral("Like"), QStringLiteral("Dislike"), QStringLiteral("Open the track in the browser")}) {
            QAction* action = FindAction(menu, text);
            QVERIFY(action);
            QVERIFY(!action->isEnabled());
        }
        menu->close();
    }

    void generalMenusUseTheCurrentTrack_data() {
        QTest::addColumn<QString>("source");
        QTest::newRow("main") << QStringLiteral("main");
        QTest::newRow("eject") << QStringLiteral("eject");
        QTest::newRow("playlist-add") << QStringLiteral("add");
        QTest::newRow("playlist-background") << QStringLiteral("background");
    }

    void generalMenusUseTheCurrentTrack() {
        QFETCH(QString, source);
        Tests::MockHttpServer server;
        QVERIFY(connectLibrary(server));
        const QList<Track> tracks = MakeTracks(3);
        application->player()->setSource(
            Core::QueueSource{.title = QStringLiteral("Test"), .head = YandexSource::TracksOf(tracks)}, false
        );
        Click(playlist, {60, 20 + 3 + 2 * 13 + 6});
        QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "recordTrackUrl");
        QMenu* menu = nullptr;
        if (source == QStringLiteral("main")) {
            menu = OpenContextMenu(main, {100, 10});
        } else if (source == QStringLiteral("eject")) {
            main->sourcesMenuRequested(QPoint(100, 100));
            menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        } else if (source == QStringLiteral("add")) {
            Click(playlist, {20, playlist->skinSize().height() - 25});
            menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        } else {
            menu = OpenContextMenu(playlist, {60, 150});
        }
        QVERIFY(menu);
        QAction* action = FindAction(menu, QStringLiteral("Open the track in the browser"));
        QVERIFY(action);
        action->trigger();
        menu->close();
        QCOMPARE(openedUrl, tracks[0].webUrl());
    }

    void dislikeOnlySkipsWhileItsTrackIsCurrent_data() {
        QTest::addColumn<bool>("advanceBeforeAction");
        QTest::newRow("current") << false;
        QTest::newRow("previous") << true;
    }

    void dislikeOnlySkipsWhileItsTrackIsCurrent() {
        QFETCH(bool, advanceBeforeAction);
        Tests::MockHttpServer server;
        QVERIFY(connectLibrary(server));
        const QList<Track> tracks = MakeTracks(3);
        application->player()->setSource(
            Core::QueueSource{.title = QStringLiteral("Test"), .head = YandexSource::TracksOf(tracks)}, false
        );
        QMenu* menu = OpenContextMenu(playlist, {60, 20 + 3 + 6});
        QVERIFY(menu);
        QAction* action = FindAction(menu, QStringLiteral("Dislike (skip)"));
        QVERIFY(action);
        if (advanceBeforeAction) {
            application->player()->playIndex(1);
        }
        action->trigger();
        menu->close();
        const QString path = QStringLiteral("/users/42/dislikes/tracks/add-multiple");
        QTRY_VERIFY(server.last(path));
        QCOMPARE(server.last(path)->formValue("track-ids"), tracks[0].id);
        QCOMPARE(application->player()->currentIndex(), 1);
    }

    void smoothScrollWheelChangesVolume() {
        main->setVolume(50);
        for (int i = 0; i < 3; ++i) {  // three 40-unit touchpad deltas = one notch
            QWheelEvent wheel(
                QPointF(150, 60),
                main->mapToGlobal(QPointF(150, 60)),
                QPoint(),
                QPoint(0, 40),
                Qt::NoButton,
                Qt::NoModifier,
                Qt::NoScrollPhase,
                false
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
        application->player()->setSource(
            Core::QueueSource{.title = QStringLiteral("Test"), .head = YandexSource::TracksOf(MakeTracks(50))}, false
        );
        QCOMPARE(playlist->visibleRows(), 13);  // default height 232: (232-58)/13
        const QPoint thirdRow(60, 20 + 3 + 2 * 13 + 6);
        QCOMPARE(playlist->rowAt(thirdRow), 2);
        // Double click plays that row (no audio device here, but the index moves).
        QMouseEvent doubleClick(
            QEvent::MouseButtonDblClick,
            QPointF(thirdRow),
            playlist->mapToGlobal(QPointF(thirdRow)),
            Qt::LeftButton,
            Qt::LeftButton,
            Qt::NoModifier
        );
        QCoreApplication::sendEvent(playlist, &doubleClick);
        QCOMPARE(application->player()->currentIndex(), 2);
    }

    void playlistScrollsAndResizes() {
        application->player()->setSource(
            Core::QueueSource{.title = QStringLiteral("Test"), .head = YandexSource::TracksOf(MakeTracks(50))}, false
        );
        QWheelEvent wheel(
            QPointF(60, 60),
            playlist->mapToGlobal(QPointF(60, 60)),
            QPoint(),
            QPoint(0, -120),
            Qt::NoButton,
            Qt::NoModifier,
            Qt::NoScrollPhase,
            false
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
        application->player()->setSource(
            Core::QueueSource{.title = QStringLiteral("Test"), .head = YandexSource::TracksOf(MakeTracks(5))}, false
        );
        Click(playlist, {60, 20 + 3 + 6});  // row 0
        QKeyEvent shiftDown(QEvent::KeyPress, Qt::Key_Down, Qt::ShiftModifier);
        QCoreApplication::sendEvent(playlist, &shiftDown);
        QCOMPARE(playlist->selection().size(), 2);
        QKeyEvent deleteKey(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QCoreApplication::sendEvent(playlist, &deleteKey);
        QCOMPARE(application->player()->entries().size(), 3);
        QCOMPARE(application->player()->entries().first().track.title, QStringLiteral("Track 3"));
    }

    void eqSliderSetsItsBandAndOnButtonDisables() {
        QSignalSpy changed(eq, &Ui::EqualizerWindow::settingsChanged);
        // Band 60 Hz at x=78, slider top y=38, 51 px travel: top = +12 dB.
        const QPoint top(78 + 7, 38 + 5);
        SendMouseEvent(eq, QEvent::MouseButtonPress, top, eq->mapToGlobal(top), Qt::LeftButton, Qt::LeftButton);
        SendMouseEvent(eq, QEvent::MouseButtonRelease, top, eq->mapToGlobal(top), Qt::LeftButton, Qt::NoButton);
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
                const int neededHeight =
                    label->wordWrap() ? label->heightForWidth(label->width()) : label->sizeHint().height();
                if (label->height() < neededHeight) {
                    qWarning("label %s: %d < %d", qPrintable(label->text().left(30)), label->height(), neededHeight);
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
        application->player()->setSource(
            Core::QueueSource{.title = QStringLiteral("A"), .head = YandexSource::TracksOf(MakeTracks(3))}, false
        );
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
            application->windows()->snapshot().save(qEnvironmentVariable("QIYAA_TEST_SHOTS") + "/shaded.png");
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
        application->windows()->setVisible(application->playlistWindow(), false);
        main->setShaded(true);
        application->windows()->setVisible(application->playlistWindow(), true);
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
        application->player()->setSource(
            Core::QueueSource{.title = QStringLiteral("A"), .head = YandexSource::TracksOf(MakeTracks(40))}, false
        );
        playlist->setShaded(true);
        application->player()->playIndex(39);  // scrolls the one-row shaded view to row 39
        playlist->setShaded(false);
        QCOMPARE(playlist->scrollOffset(), 40 - 13);  // 13 rows fit the default height; last row at the bottom
    }

    void unshadingAtTheBottomStaysOnScreen() {
        main->setShaded(true);
        eq->setShaded(true);
        playlist->setShaded(true);
        const QRect screen = main->screen()->availableGeometry();
        const int y = screen.y() + screen.height() - 42;  // the shaded stack sits on the bottom edge
        main->move(main->x(), y);
        eq->move(main->x(), y + 14);
        playlist->move(main->x(), y + 28);
        main->setShaded(false);
        QVERIFY(screen.contains(playlist->frameGeometry()));
        QCOMPARE(eq->pos(), main->pos() + QPoint(0, 116));
        QCOMPARE(playlist->pos(), eq->pos() + QPoint(0, 14));
    }

    void nothingPlaysAfterShutDown() {
        application->player()->setSource(
            Core::QueueSource{.title = QStringLiteral("A"), .head = YandexSource::TracksOf(MakeTracks(3))}, false
        );
        application->player()->shutDown();
        application->player()->playIndex(2);
        QCOMPARE(application->player()->currentIndex(), 0);
    }

    void milkdropWindowOpensBesideTheEqualizer() {
        Ui::MilkdropWindow* milkdrop = application->milkdropWindow();
#if defined(QIYAA_HAVE_MILKDROP)
        QVERIFY(milkdrop);
        QVERIFY(!milkdrop->isVisible());
        application->windows()->setVisible(application->milkdropWindow(), true);
        QVERIFY(milkdrop->isVisible());
        QCOMPARE(milkdrop->pos(), main->pos() + QPoint(main->width(), main->height()));  // right of the equalizer
        QVERIFY(milkdrop->presets().size() >= 50);
        if (!qEnvironmentVariableIsEmpty("QIYAA_TEST_SHOTS")) {
            application->windows()->snapshot().save(qEnvironmentVariable("QIYAA_TEST_SHOTS") + "/milkdrop-window.png");
        }
        application->windows()->setVisible(application->milkdropWindow(), false);
        QVERIFY(!milkdrop->isVisible());
#else
        QVERIFY(!milkdrop);
        application->windows()->setVisible(application->milkdropWindow(), true);  // no-op
#endif
    }

    void doubleClickTitleShades() {
        QMouseEvent doubleClick(
            QEvent::MouseButtonDblClick,
            QPointF(100, 5),
            main->mapToGlobal(QPointF(100, 5)),
            Qt::LeftButton,
            Qt::LeftButton,
            Qt::NoModifier
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
            eq, QEvent::MouseButtonPress, volumeRight, eq->mapToGlobal(volumeRight), Qt::LeftButton, Qt::LeftButton
        );
        SendMouseEvent(
            eq, QEvent::MouseButtonRelease, volumeRight, eq->mapToGlobal(volumeRight), Qt::LeftButton, Qt::NoButton
        );
        QCOMPARE(main->volume(), 100);
        const QPoint balanceLeft(164, 7);  // left end of the mini balance slider
        SendMouseEvent(
            eq, QEvent::MouseButtonPress, balanceLeft, eq->mapToGlobal(balanceLeft), Qt::LeftButton, Qt::LeftButton
        );
        SendMouseEvent(
            eq, QEvent::MouseButtonRelease, balanceLeft, eq->mapToGlobal(balanceLeft), Qt::LeftButton, Qt::NoButton
        );
        QCOMPARE(main->balance(), -100);
    }

    void mainShadeTransportWorks() {
        application->player()->setSource(
            Core::QueueSource{.title = QStringLiteral("A"), .head = YandexSource::TracksOf(MakeTracks(3))}, false
        );
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
        server.on("GET", "/cover/400x400", [png](const Tests::MockRequest&) { return Tests::MockResponse{200, png}; });

        QList<Yandex::Track> tracks = MakeTracks(1);
        tracks[0].title = QStringLiteral("Группа крови");
        tracks[0].artists = {QStringLiteral("Кино")};
        tracks[0].albumTitle = QStringLiteral("Группа крови");
        tracks[0].year = 1988;
        tracks[0].coverUri = server.baseUrl() + QStringLiteral("/cover/%%");
        application->player()->setSource(
            Core::QueueSource{.title = QStringLiteral("A"), .head = YandexSource::TracksOf(tracks)}, false
        );
        application->windows()->setVisible(application->nowPlayingWindow(), true);
        Ui::NowPlayingWindow* nowPlaying = application->nowPlayingWindow();
        QVERIFY(QTest::qWaitForWindowExposed(nowPlaying));
        QVERIFY(
            QTest::qWaitFor([&] { return !application->covers()->localFile(tracks[0].coverUrl(400)).isEmpty(); }, 5000)
        );
        QApplication::processEvents();
        const QImage shot = nowPlaying->grab().toImage();
        const QRect coverArea = nowPlaying->coverRect();
        QCOMPARE(QColor(shot.pixel(coverArea.center())), QColor(Qt::red));
        if (!qEnvironmentVariableIsEmpty("QIYAA_TEST_SHOTS")) {
            application->windows()->snapshot().save(qEnvironmentVariable("QIYAA_TEST_SHOTS") + "/nowplaying.png");
        }
        QCOMPARE(nowPlaying->pos(), main->pos() + QPoint(main->width(), 0));
    }

    void snapshotContainsAllWindows() {
        const QImage image = application->windows()->snapshot();
        QCOMPARE(image.width(), 275);
        QCOMPARE(image.height(), 116 + 116 + 232);
    }

    void aFreshStartWritesNoSetting() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString settingsFile = directory.filePath(QStringLiteral("settings.ini"));
        {
            App::Application fresh(OptionsWithSettings(settingsFile));
            fresh.start();
            QVERIFY(QTest::qWaitForWindowExposed(fresh.mainWindow()));
        }
        QVERIFY(AllSettings(settingsFile).isEmpty());
    }

    void aRestartBringsBackShadesStepsAndPositions() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString settingsFile = directory.filePath(QStringLiteral("settings.ini"));
        {
            QSettings saved(settingsFile, QSettings::IniFormat);
            saved.setValue(QStringLiteral("mainWindow/pos"), QPoint(40, 30));
            saved.setValue(QStringLiteral("equalizer/pos"), QPoint(315, 30));
            saved.setValue(QStringLiteral("equalizer/shaded"), true);
            saved.setValue(QStringLiteral("playlist/pos"), QPoint(40, 146));
            saved.setValue(QStringLiteral("playlist/steps"), QSize(1, 2));
        }
        const QVariantMap before = AllSettings(settingsFile);
        {
            App::Application restarted(OptionsWithSettings(settingsFile));
            restarted.start();
            QCOMPARE(restarted.mainWindow()->pos(), QPoint(40, 30));
            QCOMPARE(restarted.equalizerWindow()->pos(), QPoint(315, 30));
            QVERIFY(restarted.equalizerWindow()->isShaded());
            QCOMPARE(restarted.equalizerWindow()->height(), 14);
            QCOMPARE(restarted.playlistWindow()->pos(), QPoint(40, 146));
            QCOMPARE(restarted.playlistWindow()->size(), QSize(300, 174));
        }
        QCOMPARE(AllSettings(settingsFile), before);
    }

    void minimisingHidesTheOtherWindowsAndRestoringShowsTheChosenOnes() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString settingsFile = directory.filePath(QStringLiteral("settings.ini"));
        {
            QSettings saved(settingsFile, QSettings::IniFormat);
            saved.setValue(QStringLiteral("playlist/visible"), false);
            saved.setValue(QStringLiteral("nowPlaying/visible"), true);
            saved.setValue(QStringLiteral("jamWindow/visible"), true);
        }
        App::Application started(OptionsWithSettings(settingsFile));
        started.start();
        const auto jamWindowShown = [&started] { return started.jamWindow() && started.jamWindow()->isVisible(); };
        QVERIFY(started.equalizerWindow()->isVisible());
        QVERIFY(!started.playlistWindow()->isVisible());
        QVERIFY(started.nowPlayingWindow()->isVisible());
        QVERIFY(!jamWindowShown());  // the start leaves it to the menu and to "Continue"
        started.mainWindow()->minimizedChanged(true);
        QVERIFY(!started.equalizerWindow()->isVisible());
        QVERIFY(!started.nowPlayingWindow()->isVisible());
        started.mainWindow()->minimizedChanged(false);
        QVERIFY(started.equalizerWindow()->isVisible());
        QVERIFY(!started.playlistWindow()->isVisible());
        QVERIFY(started.nowPlayingWindow()->isVisible());
        QCOMPARE(jamWindowShown(), started.jamWindow() != nullptr);  // restoring does show it
    }

    void aScaleForThisRunLeavesThePositionsUnsaved() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString settingsFile = directory.filePath(QStringLiteral("settings.ini"));
        App::Application started(OptionsWithSettings(settingsFile));
        started.start();
        started.windows()->setScale(1.5, App::WindowSet::ScaleScope::ThisRun);
        started.saveState();
        const QSettings saved(settingsFile, QSettings::IniFormat);
        QVERIFY(!saved.contains(QStringLiteral("scale")));
        QVERIFY(!saved.contains(QStringLiteral("mainWindow/pos")));
        started.windows()->setScale(1.5);
        QCOMPARE(saved.value(QStringLiteral("scale")).toDouble(), 1.5);
        QVERIFY(saved.contains(QStringLiteral("mainWindow/pos")));
        QVERIFY(saved.contains(QStringLiteral("equalizer/pos")));
    }

    void theJamWindowIsOneOfTheWindows() {
#ifdef QIYAA_HAVE_JAM
        Ui::JamWindow* jam = application->jamWindow();
        QVERIFY(jam);
        QVERIFY(!jam->isVisible());
        application->windows()->setVisible(application->jamWindow(), true);
        QVERIFY(jam->isVisible());
        QCOMPARE(jam->skinSize(), QSize(300, 348));
        application->windows()->setScale(2.0);
        QCOMPARE(jam->scale(), 2.0);
        application->windows()->setScale(1.0);
        application->windows()->setVisible(application->jamWindow(), false);
        QVERIFY(!jam->isVisible());
#else
        QSKIP("built without the jam");
#endif
    }

    void duringAJamThePlaylistEditsTheBase() {  // HOST-36
#ifdef QIYAA_HAVE_JAM
        Core::Player* player = application->player();
        const auto ids = [player] {
            QStringList found;
            for (const Core::QueueEntry& entry : player->entries()) {
                found << YandexSource::CatalogId(entry.track.key);
            }
            return found;
        };
        const QList<Core::Track> tracks = YandexSource::TracksOf(MakeTracks(5));
        player->setSource(Core::QueueSource{.title = QStringLiteral("A"), .head = tracks.mid(0, 4)}, false);
        application->jam()->start(false);
        QVERIFY(application->jam()->isActive());
        application->jam()->setQueue({Core::JamEntry {.itemId = QStringLiteral("i1"), .track = tracks[4] }}, {}, 0);
        QCOMPARE(ids(), (QStringList{"1", "5", "2", "3", "4"}));
        QKeyEvent deleteKey(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        Click(playlist, {60, 20 + 3 + 6 + 2 * 13});  // row 2, a base track
        QCoreApplication::sendEvent(playlist, &deleteKey);
        QCOMPARE(ids(), (QStringList{"1", "5", "3", "4"}));
        QSignalSpy offline(application->jamHost(), &Jam::HostSession::offline);
        Click(playlist, {60, 20 + 3 + 6 + 13});  // row 1, the item: only the server removes it
        QCoreApplication::sendEvent(playlist, &deleteKey);
        QCOMPARE(ids(), (QStringList{"1", "5", "3", "4"}));
        QCOMPARE(offline.size(), 1);
        player->clear();
        QCOMPARE(ids(), (QStringList{"1", "5"}));
        QCOMPARE(player->currentIndex(), 0);
        application->jam()->end();
#else
        QSKIP("built without the jam");
#endif
    }

    void continuingAStoredJamResumesItAndShowsTheJamWindow() {  // HOST-23
#ifdef QIYAA_HAVE_JAM
        StoredJamStart start;
        QVERIFY(start.question);
        QCOMPARE(start.question->text(), QStringLiteral("Continue it? The guests' link stays the same."));
        QVERIFY(start.loggedIn());
        start.answer(QStringLiteral("Continue"));
        QCOMPARE(start.application->jamHost()->phase(), Jam::HostPhase::Active);
        QVERIFY(start.application->jamWindow()->isVisible());
        QVERIFY(start.resumed());
        QCOMPARE(start.likesAsked, 0);  // the base stays empty
        QVERIFY(start.application->player()->entries().isEmpty());
#else
        QSKIP("built without the jam");
#endif
    }

    void endingAStoredJamQueuesTheLikes() {  // HOST-23
#ifdef QIYAA_HAVE_JAM
        StoredJamStart start;
        QVERIFY(start.question);
        QVERIFY(start.loggedIn());
        QCOMPARE(start.likesAsked, 0);  // not while the question waits
        start.answer(QStringLiteral("End it"));
        QVERIFY(!start.application->jamHost()->hasStoredSession());
        QVERIFY(!start.application->jam()->isActive());
        QTRY_COMPARE(start.application->player()->entries().size(), 2);
        QCOMPARE(start.likesAsked, 1);
        QVERIFY(start.resumed());  // only to end it
#else
        QSKIP("built without the jam");
#endif
    }

    void theAccountAndJamItemsAskOnlyAfterTheMenuIsGone() {
#ifdef QIYAA_HAVE_JAM
        StoredJamStart start;
        QVERIFY(start.question);
        QVERIFY(start.loggedIn());
        start.answer(QStringLiteral("Continue"));
        QCOMPARE(start.application->jamHost()->phase(), Jam::HostPhase::Active);
        const auto mainItem = [&start](const QString& textOrName) {
            Q_EMIT start.application->mainWindow()->menuRequested(QPoint(100, 100));
            return OpenMenuItem(textOrName);
        };
        QCOMPARE(DialogAfterChoosing(mainItem(QStringLiteral("Search…"))), QStringLiteral("Search"));
        QCOMPARE(
            DialogAfterChoosing(mainItem(QStringLiteral("endJam"))),
            QStringLiteral("End the jam? The guests will see that it is over.")
        );
        QCOMPARE(start.application->jamHost()->phase(), Jam::HostPhase::Active);
#else
        QSKIP("built without the jam");
#endif
    }

    void aStoredJamClosedWithoutAnAnswerStaysStored() {  // HOST-23
#ifdef QIYAA_HAVE_JAM
        StoredJamStart start;
        QVERIFY(start.question);
        QVERIFY(start.loggedIn());
        start.question->reject();
        QTRY_VERIFY(!start.question);
        QVERIFY(start.application->jamHost()->hasStoredSession());
        QCOMPARE(start.application->jamHost()->phase(), Jam::HostPhase::None);
        QCOMPARE(start.jamServer.count(), 0);
#else
        QSKIP("built without the jam");
#endif
    }
};

QTEST_MAIN(TestWindows)
#include "windows_test.moc"
