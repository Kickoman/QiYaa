#include "app/spotify_account.h"
#include "spotify/client/credentials_file.h"
#include "spotify/client/identity.h"
#include "spotify/client/session.h"
#include "spotify/client/web.h"
#include "support/fake_access_point.h"
#include "support/mock_http_server.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <optional>
#include <string>

namespace {

const QString kDeviceId = QString(40, u'd');

SpotifyClient::Endpoints EndpointsOf(const Tests::MockHttpServer& server) {
    SpotifyClient::Endpoints endpoints;
    endpoints.apresolve = server.baseUrl();
    endpoints.accounts = server.baseUrl();
    endpoints.clientToken = server.baseUrl();
    endpoints.login5 = server.baseUrl();
    endpoints.spclientScheme = QStringLiteral("http");
    return endpoints;
}

struct Stack {
    Tests::MockHttpServer http;
    Tests::FakeAccessPointServer accessPoint;
    QNetworkAccessManager network;
    SpotifyClient::Session
        session{&network, EndpointsOf(http), SpotifyClient::Identity(QStringLiteral("0.8.0"), kDeviceId)};
    SpotifyClient::Web web{&network, EndpointsOf(http), &session, kDeviceId, QStringLiteral("0.8.0")};
    QTemporaryDir directory;
    QString file = directory.filePath(QStringLiteral("spotify-credentials"));
    App::SpotifyAccount account{App::SpotifyAccount::Parts{&session, &web, &network, nullptr, EndpointsOf(http)}, file};
    QStringList texts;

    Stack() {
        session.setServerKey(Tests::FakeAccessPointKey());
        session.setReconnectDelaysMs({50});
        http.json(
            "GET",
            QStringLiteral("/"),
            QJsonDocument(QJsonObject{{QStringLiteral("accesspoint"), QJsonArray{accessPoint.address()}}}).toJson()
        );
        QObject::connect(&account, &App::SpotifyAccount::statusText, [this](const QString& text) { texts << text; });
    }

    void save(const SpotifyProtocol::TBytes& data) {
        SpotifyClient::SaveCredentials(
            file,
            SpotifyProtocol::Credentials{
                .username = "owner",
                .type = SpotifyProtocol::AuthenticationType::StoredCredentials,
                .data = data,
            }
        );
    }
};

}  // namespace

class TestSpotifyAccount : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void nothingHappensWithoutSavedCredentials() {
        Stack stack;
        stack.account.start();
        QTest::qWait(100);
        QCOMPARE(stack.accessPoint.connectionCount(), 0);
        QVERIFY(stack.texts.isEmpty());
    }

    void savedCredentialsLogInAndAreKeptUpToDate() {  // SPOT-04, SPOT-03
        Stack stack;
        stack.save({1, 2, 3});
        stack.accessPoint.welcomeBlob = {5, 6};
        QSignalSpy ways(&stack.account, &App::SpotifyAccount::loggedInWith);
        QVERIFY(stack.account.hasSavedLogin());
        stack.account.start();
        QTRY_VERIFY(stack.account.loggedIn());
        QVERIFY(ways.isEmpty());
        QTRY_COMPARE(stack.texts.value(0), QStringLiteral("Logged in to Spotify as owner"));
        QCOMPARE(SpotifyClient::LoadCredentials(stack.file)->data, (SpotifyProtocol::TBytes{5, 6}));
    }

    void aTokenLoginSavesTheReusableCredentialsNotTheToken() {  // SPOT-03
        Stack stack;
        stack.accessPoint.welcomeBlob = {7, 7};
        QSignalSpy ways(&stack.account, &App::SpotifyAccount::loggedInWith);
        QVERIFY(!stack.account.hasSavedLogin());
        stack.account.loginWithToken(QStringLiteral("BQ-token"), App::SpotifyAccount::Way::Browser);
        QTRY_VERIFY(stack.account.loggedIn());
        QCOMPARE(ways.size(), 1);
        QCOMPARE(ways.first().at(0).value<App::SpotifyAccount::Way>(), App::SpotifyAccount::Way::Browser);
        const std::optional<SpotifyProtocol::Credentials> saved = SpotifyClient::LoadCredentials(stack.file);
        QVERIFY(saved);
        QCOMPARE(saved->data, (SpotifyProtocol::TBytes{7, 7}));
        QFile file(stack.file);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("BQ-token"));
    }

    void expiredCredentialsAreForgotten() {  // SPOT-04
        Stack stack;
        stack.save({1, 2, 3});
        stack.accessPoint.loginRefusal = static_cast<int>(SpotifyProtocol::LoginError::BadCredentials);
        QSignalSpy refusals(&stack.account, &App::SpotifyAccount::refused);
        stack.account.start();
        QTRY_COMPARE(stack.texts.value(0), QStringLiteral("The Spotify login has expired: log in again"));
        QCOMPARE(refusals.size(), 1);
        QCOMPARE(refusals.first().at(0).value<App::SpotifyAccount::Refusal>(), App::SpotifyAccount::Refusal::Expired);
        QVERIFY(!QFile::exists(stack.file));
    }

    void anAccountWithoutPremiumIsToldSo() {  // SPOT-05
        Stack stack;
        stack.accessPoint.loginRefusal = static_cast<int>(SpotifyProtocol::LoginError::PremiumAccountRequired);
        QSignalSpy refusals(&stack.account, &App::SpotifyAccount::refused);
        QSignalSpy ways(&stack.account, &App::SpotifyAccount::loggedInWith);
        stack.account.loginWithToken(QStringLiteral("BQ-token"));
        QTRY_VERIFY(stack.texts.contains(QStringLiteral("Spotify Premium is needed to play")));
        QCOMPARE(refusals.size(), 1);
        QCOMPARE(
            refusals.first().at(0).value<App::SpotifyAccount::Refusal>(), App::SpotifyAccount::Refusal::PremiumRequired
        );
        QVERIFY(ways.isEmpty());
        QVERIFY(!QFile::exists(stack.file));
    }

    void logoutForgetsTheCredentials() {  // SPOT-06
        Stack stack;
        stack.save({1, 2, 3});
        stack.account.start();
        QTRY_VERIFY(stack.account.loggedIn());
        QSignalSpy loggedOut(&stack.account, &App::SpotifyAccount::loggedOut);
        stack.account.logout();
        QCOMPARE(loggedOut.size(), 1);
        QVERIFY(!stack.account.hasSavedLogin());
        QVERIFY(!stack.account.loggedIn());
        QVERIFY(!QFile::exists(stack.file));
        QCOMPARE(stack.texts.last(), QStringLiteral("You have logged out of Spotify"));
    }
};

QTEST_GUILESS_MAIN(TestSpotifyAccount)
#include "spotify_account_test.moc"
