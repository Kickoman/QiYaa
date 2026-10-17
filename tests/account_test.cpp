#include "app/account.h"
#include "audio/audio_engine.h"
#include "core/player.h"
#include "core/source_notice.h"
#include "sources/yandex/catalog.h"
#include "support/mock_http_server.h"
#include "support/yandex_sources.h"
#include "yandex/api_client.h"
#include "yandex/library.h"

#include <QFile>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

// App::Account without windows: the login and logout on a MockHttpServer, the token files in
// temporary folders, and the rule that a login queues the likes into an empty queue (the
// catalogue's Loading notice is the ask, at once).

class TestAccount : public QObject {
    Q_OBJECT
private:
    struct Stack {
        Tests::MockHttpServer server;
        Tests::LocalNetworkAccessManager network;
        Yandex::ApiClient api{&network};
        Yandex::Library library{&api};
        Audio::AudioEngine engine;  // not initialised
        Tests::YandexSources ports{&library};
        Core::Player player{&engine, &ports.registry};
        YandexSource::Catalog catalog{&player, &library};
        QTemporaryDir directory;
        QTemporaryDir yaampDirectory;
        App::Account account{
            App::Account::Parts{&api, &library, &player, &catalog, &network, nullptr},
            App::Account::TokenFiles{
                directory.filePath(QStringLiteral("token")),
                {yaampDirectory.filePath(QStringLiteral("token.json"))},
                directory.path()
            }
        };
        QStringList texts;
        int likesAsked = 0;

        explicit Stack(const QString& accountStatus = QStringLiteral("account-status/ok")) {
            api.setBaseUrl(server.baseUrl());
            server.fixture("GET", "/account/status", accountStatus);
            server.fixture("GET", "/users/42/likes/tracks", "users-likes-tracks/string-ids");
            server.fixture("POST", "/tracks/", "tracks/two-tracks");
            QObject::connect(&account, &App::Account::statusText, [this](const QString& text) { texts << text; });
            QObject::connect(&catalog, &YandexSource::Catalog::noticed, [this](const Core::SourceNotice& notice) {
                likesAsked += notice.code == Core::SourceNoticeCode::Loading ? 1 : 0;
            });
        }

        bool loggedIn() {
            return QTest::qWaitFor([this] { return library.isLoggedIn(); }, 5000);
        }

        QByteArray ownToken() const {
            QFile file(directory.filePath(QStringLiteral("token")));
            return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray();
        }
    };

private Q_SLOTS:
    void initTestCase() { qunsetenv("QIYAA_TOKEN"); }

    void aLoginGreetsAndQueuesTheLikesIntoAnEmptyQueue() {
        Stack stack;
        QSignalSpy connected(&stack.account, &App::Account::connected);
        stack.account.applyToken(QStringLiteral("test-token"), false);
        QVERIFY(stack.loggedIn());
        QCOMPARE(connected.size(), 1);
        QCOMPARE(connected.at(0).at(0).toString(), QStringLiteral("Kick"));
        QCOMPARE(stack.texts, (QStringList{"Connecting to Yandex Music…", "Hello, Kick!"}));
        QTRY_COMPARE(stack.player.entries().size(), 2);
        QCOMPARE(stack.likesAsked, 1);
        QVERIFY(stack.ownToken().isEmpty());
    }

    void aLoginKeepsAQueueAndHeldLikes() {
        Stack held;
        held.account.setLikesOnHold([] { return true; });
        held.account.applyToken(QStringLiteral("test-token"), false);
        QVERIFY(held.loggedIn());
        QCOMPARE(held.likesAsked, 0);
        held.account.queueLikes();  // what "End it" does: the hold is the login's only
        QTRY_COMPARE(held.player.entries().size(), 2);
        QCOMPARE(held.likesAsked, 1);

        Stack busy;
        busy.player.setSource(Core::QueueSource{.title = QStringLiteral("Mine"), .head = {Core::Track{}}}, false);
        busy.account.applyToken(QStringLiteral("test-token"), true);
        QVERIFY(busy.loggedIn());
        QCOMPARE(busy.likesAsked, 0);
        QCOMPARE(busy.ownToken(), QByteArray("test-token"));
    }

    void aFailedLoginSaysWhy() {
        Stack stack(QStringLiteral("account-status/500-empty"));
        QSignalSpy failed(&stack.account, &App::Account::failed);
        stack.account.applyToken(QStringLiteral("test-token"), true);
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(stack.texts.size(), 2);
        QVERIFY(stack.texts.at(1).startsWith(QStringLiteral("Login failed: ")));
        QVERIFY(!stack.library.isLoggedIn());
        QVERIFY(stack.ownToken().isEmpty());
        QCOMPARE(stack.likesAsked, 0);
    }

    void aTokenOfTheOldYaampIsCopiedToTheOwnFile() {
        Stack stack;
        QFile yaamp(stack.yaampDirectory.filePath(QStringLiteral("token.json")));
        QVERIFY(yaamp.open(QIODevice::WriteOnly));
        yaamp.write(R"({"access_token": "yaamp-token"})");
        yaamp.close();
        stack.account.start();
        QVERIFY(stack.loggedIn());
        QCOMPARE(stack.ownToken(), QByteArray("yaamp-token"));
    }

    void loggingOutEndsTheJamFirstThenForgetsTheToken() {
        Stack stack;
        stack.account.applyToken(QStringLiteral("test-token"), true);
        QVERIFY(stack.loggedIn());
        QTRY_COMPARE(stack.player.entries().size(), 2);
        qsizetype entriesWhenAsked = -1;
        QObject::connect(&stack.account, &App::Account::loggingOut, [&] {
            entriesWhenAsked = stack.player.entries().size();
        });
        stack.account.logout();
        QCOMPARE(entriesWhenAsked, 2);  // HOST-32: the jam ends while the account still works
        QVERIFY(stack.player.entries().isEmpty());
        QVERIFY(!stack.library.isLoggedIn());
        QVERIFY(stack.ownToken().isEmpty());
        QCOMPARE(stack.texts.last(), QStringLiteral("You have logged out"));
    }
};

QTEST_GUILESS_MAIN(TestAccount)
#include "account_test.moc"
