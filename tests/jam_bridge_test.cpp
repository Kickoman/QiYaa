#include "app/account.h"
#include "app/jam_bridge.h"
#include "app/settings.h"
#include "app/window_set.h"
#include "audio/audio_engine.h"
#include "core/jam_mode.h"
#include "core/player.h"
#include "core/source_notice.h"
#include "jam/host_session.h"
#include "jam/session_store.h"
#include "skins/skin.h"
#include "sources/yandex/catalog.h"
#include "support/jam_stub_server.h"
#include "support/mock_http_server.h"
#include "support/spec_fixtures.h"
#include "support/yandex_sources.h"
#include "ui/jam_window.h"
#include "ui/main_window.h"
#include "ui/playlist_window.h"
#include "ui/search_window.h"
#include "yandex/api_client.h"
#include "yandex/library.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QWebSocket>

// App::JamBridge against spec/jam/host.md: the words the app gives the host session's signals, and
// what the account means to the jam, on a JamStubServer.

namespace {

QByteArray Message(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

}  // namespace

class TestJamBridge : public QObject {
    Q_OBJECT
private:
    Tests::MockHttpServer catalogServer;
    Tests::LocalNetworkAccessManager networkManager;
    Yandex::ApiClient api{&networkManager};
    Yandex::Library library{&api};
    Skins::Skin baseSkin = Skins::Skin::BuiltinBase();

    struct Stack {
        Tests::JamStubServer server;
        QTemporaryDir directory;
        int ids = 0;
        Audio::AudioEngine engine;
        Tests::YandexSources ports;
        Core::Player player;
        YandexSource::Catalog catalog;
        Core::JamMode jam;
        Jam::HostSession host;
        Ui::MainWindow mainWindow;
        Ui::PlaylistWindow playlistWindow;
        Ui::JamWindow window;
        Ui::SearchWindow search;
        App::Settings settings;
        App::WindowSet windows;
        App::Account account;
        App::JamBridge bridge;
        QStringList texts;
        int likesAsked = 0;

        Stack(Yandex::ApiClient* api, Yandex::Library* library, const Skins::Skin* skin)
            : ports(library)
            , player(&engine, &ports.registry)
            , catalog(&player, library)
            , jam(&player)
            , host(&jam, library, Jam::SessionStore(directory.filePath(QStringLiteral("jam.json"))), options())
            , mainWindow(&player, &engine, skin)
            , playlistWindow(&player, skin)
            , window(&host, skin)
            , search({{"Yandex Music", &catalog}}, &player, skin)
            , settings(directory.filePath(QStringLiteral("settings.ini")))
            , windows(&settings, &mainWindow, App::WindowSet::Spec{.group = QStringLiteral("mainWindow")})
            , account(
                  App::Account::Parts{api, library, &player, &catalog, nullptr, nullptr},
                  App::Account::TokenFiles{directory.filePath(QStringLiteral("token")), {}, directory.path()}
              )
            , bridge(App::JamBridge::Parts{
                  &host,
                  &window,
                  &jam,
                  &player,
                  &settings,
                  &windows,
                  &account,
                  &mainWindow,
                  &playlistWindow,
                  &search
              }) {
            windows.add(&window, App::WindowSet::Spec{.group = QStringLiteral("jamWindow")});
            windows.add(
                &search, App::WindowSet::Spec{.group = QStringLiteral("search"), .stepsByDefault = QSize(3, 12)}
            );
            QObject::connect(&bridge, &App::JamBridge::statusText, [this](const QString& text) { texts << text; });
            QObject::connect(&catalog, &YandexSource::Catalog::noticed, [this](const Core::SourceNotice& notice) {
                likesAsked += notice.code == Core::SourceNoticeCode::Loading ? 1 : 0;
            });
        }

        Jam::HostOptions options() {
            Jam::HostOptions options;
            options.client.appVersion = QStringLiteral("test");
            options.client.reconnectDelaysMs = {50, 100, 200};
            options.config = [this] { return Jam::HostConfig{server.serverUrl(), false}; };
            options.newId = [this] { return QStringLiteral("r%1").arg(++ids); };
            return options;
        }

        // Waits for the next connection's hello and welcomes it.
        bool open(int connections) {
            if (!QTest::qWaitFor([&] { return server.count() == connections && !server.received().isEmpty(); }, 5000)) {
                return false;
            }
            server.welcome();
            return QTest::qWaitFor([&] { return server.received().size() >= 2; }, 5000);
        }

        bool created() {
            if (!host.create(QStringLiteral("Маша")) || !open(1)) {
                return false;
            }
            QJsonObject created = Tests::SpecObject(QStringLiteral("jam/protocol/examples/created/ok.json"));
            created[QStringLiteral("id")] = QStringLiteral("r1");
            server.send(Message(created));
            return QTest::qWaitFor([&] { return host.isConnected(); }, 5000);
        }

        std::optional<Ui::SearchWindow::Control> searchControl(const QString& label, int skip = 0) {
            for (const Ui::SearchWindow::Control& control : search.controls()) {
                if (control.label == label && skip-- == 0) {
                    return control;
                }
            }
            return std::nullopt;
        }

        QJsonObject lastSent(const QString& type) const {
            const QStringList received = server.received();
            for (auto it = received.crbegin(); it != received.crend(); ++it) {
                const QJsonObject message = QJsonDocument::fromJson(it->toUtf8()).object();
                if (message.value(QStringLiteral("type")).toString() == type) {
                    return message;
                }
            }
            return {};
        }

        void refuse(const QString& id, const QString& reason) {
            server.send(Message(
                {{QStringLiteral("type"), QStringLiteral("rejected")},
                 {QStringLiteral("id"), id},
                 {QStringLiteral("reason"), reason}}
            ));
        }
    };

private Q_SLOTS:
    void initTestCase() {
        api.setBaseUrl(catalogServer.baseUrl());
        catalogServer.fixture("GET", "/account/status", "account-status/ok");
        catalogServer.fixture("GET", "/users/42/likes/tracks", "users-likes-tracks/string-ids");
        catalogServer.fixture("POST", "/tracks/", "tracks/two-tracks");
    }

    void theConnectionIsWordedWhileTheJamIsOn() {  // HOST-25, HOST-26
        Stack stack(&api, &library, &baseSkin);
        QVERIFY(stack.created());
        QCOMPARE(stack.texts, QStringList{QStringLiteral("The jam is connected")});
        stack.server.last().close();
        QTRY_COMPARE(stack.texts.size(), 2);
        QCOMPARE(stack.texts.last(), QStringLiteral("No connection to the jam server · trying again by itself"));
        stack.host.end();
        QCOMPARE(stack.texts.last(), QStringLiteral("The jam is over"));
    }

    void aRefusalIsWordedByItsReason() {
        Stack stack(&api, &library, &baseSkin);
        stack.host.create(QStringLiteral("Маша"));
        QVERIFY(stack.open(1));
        stack.refuse(QStringLiteral("r1"), QStringLiteral("rate-limited"));
        QTRY_COMPARE(stack.texts, QStringList{QStringLiteral("Too many requests, wait a little")});
        QTRY_COMPARE(stack.host.phase(), Jam::HostPhase::None);
        stack.host.create(QStringLiteral("Маша"));
        QVERIFY(stack.open(2));
        stack.refuse(QStringLiteral("r2"), QStringLiteral("no-such-reason"));
        QTRY_COMPARE(stack.texts.size(), 2);
        QCOMPARE(stack.texts.last(), QStringLiteral("The jam server refused"));
    }

    void anEndByTheServerIsWorded() {
        Stack stack(&api, &library, &baseSkin);
        QVERIFY(stack.created());
        stack.server.send(Message(
            {{QStringLiteral("type"), QStringLiteral("ended")}, {QStringLiteral("reason"), QStringLiteral("host-ended")}
            }
        ));
        QTRY_COMPARE(stack.host.phase(), Jam::HostPhase::None);
        QCOMPARE(stack.texts.last(), QStringLiteral("The jam was ended"));
    }

    void theLikesWaitWhileTheJamIsOnAndLoggingOutEndsIt() {  // HOST-32
        Stack stack(&api, &library, &baseSkin);
        QVERIFY(stack.created());
        stack.account.applyToken(QStringLiteral("test-token"), false);
        QVERIFY(QTest::qWaitFor([&] { return library.isLoggedIn(); }, 5000));
        QCOMPARE(stack.likesAsked, 0);
        stack.account.logout();
        QCOMPARE(stack.host.phase(), Jam::HostPhase::None);
        QCOMPARE(stack.texts.last(), QStringLiteral("The jam is over"));
    }

    void theSearchWindowAddsToTheJamWhileOneRuns() {  // HOST-38, HOST-20
        Stack stack(&api, &library, &baseSkin);
        catalogServer.fixture("GET", "/search", "search/best-track");
        QVERIFY(stack.created());
        stack.account.applyToken(QStringLiteral("test-token"), false);
        QTRY_VERIFY(library.isLoggedIn());
        stack.search.search(QStringLiteral("кино"));
        QTRY_VERIFY(stack.searchControl(QStringLiteral("Add")));
        stack.search.show();
        QTest::mouseClick(&stack.search, Qt::LeftButton, {}, stack.searchControl(QStringLiteral("Add"))->rect.center());
        QTRY_VERIFY(!stack.lastSent(QStringLiteral("add")).isEmpty());
        QCOMPARE(
            stack.lastSent(QStringLiteral("add")).value(QStringLiteral("track")).toObject().value(QStringLiteral("id")),
            QStringLiteral("33311009")
        );
        QVERIFY(!stack.searchControl(QStringLiteral("Add"))->enabled);
        const QString addId = stack.lastSent(QStringLiteral("add")).value(QStringLiteral("id")).toString();
        stack.refuse(addId, QStringLiteral("duplicate"));
        QTRY_VERIFY(stack.texts.contains(QStringLiteral("This track is already in the jam queue")));
        QVERIFY(stack.searchControl(QStringLiteral("Add"))->enabled);
        stack.host.end();
        QTRY_VERIFY(stack.host.phase() == Jam::HostPhase::None);
        QVERIFY(stack.searchControl(QStringLiteral("Add"))->enabled);
        stack.account.logout();
    }

    void aRefusalStaysOnTheRowOfItsAdd() {  // HOST-38
        Stack stack(&api, &library, &baseSkin);
        catalogServer.fixture("GET", "/search", "search/all-parts");
        QVERIFY(stack.created());
        stack.account.applyToken(QStringLiteral("test-token"), false);
        QTRY_VERIFY(library.isLoggedIn());
        stack.search.show();
        stack.search.search(QStringLiteral("кино"));
        QTRY_VERIFY(stack.searchControl(QStringLiteral("Add"), 1));
        QTest::mouseClick(&stack.search, Qt::LeftButton, {}, stack.searchControl(QStringLiteral("Add"))->rect.center());
        QTRY_VERIFY(!stack.lastSent(QStringLiteral("add")).isEmpty());
        const QString firstAdd = stack.lastSent(QStringLiteral("add")).value(QStringLiteral("id")).toString();
        const QPoint second = stack.searchControl(QStringLiteral("Add"), 1)->rect.center();
        QTest::mouseClick(&stack.search, Qt::LeftButton, {}, second);
        QTRY_VERIFY(stack.lastSent(QStringLiteral("add")).value(QStringLiteral("id")).toString() != firstAdd);
        stack.refuse(QStringLiteral("not-an-add"), QStringLiteral("not-allowed"));
        QTRY_VERIFY(stack.texts.contains(QStringLiteral("The jam server does not allow this")));
        QVERIFY(!stack.searchControl(QStringLiteral("Add"))->enabled);
        QVERIFY(!stack.searchControl(QStringLiteral("Add"), 1)->enabled);
        stack.refuse(firstAdd, QStringLiteral("queue-limit"));
        QTRY_VERIFY(stack.searchControl(QStringLiteral("Add"))->enabled);
        QVERIFY(!stack.searchControl(QStringLiteral("Add"), 1)->enabled);
        stack.host.end();
        QTRY_COMPARE(stack.host.phase(), Jam::HostPhase::None);
        QVERIFY(stack.searchControl(QStringLiteral("Add"), 1)->enabled);
        stack.account.logout();
    }
};

QTEST_MAIN(TestJamBridge)
#include "jam_bridge_test.moc"
