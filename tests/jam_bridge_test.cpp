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
            , window(&host, &catalog, skin)
            , settings(directory.filePath(QStringLiteral("settings.ini")))
            , windows(&settings, &mainWindow, App::WindowSet::Spec{.group = QStringLiteral("mainWindow")})
            , account(
                  App::Account::Parts{api, library, &player, &catalog, nullptr, nullptr},
                  App::Account::TokenFiles{directory.filePath(QStringLiteral("token")), {}, directory.path()}
              )
            , bridge(
                  App::JamBridge::
                      Parts{&host, &window, &jam, &player, &settings, &windows, &account, &mainWindow, &playlistWindow}
              ) {
            windows.add(&window, App::WindowSet::Spec{.group = QStringLiteral("jamWindow")});
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
};

QTEST_MAIN(TestJamBridge)
#include "jam_bridge_test.moc"
