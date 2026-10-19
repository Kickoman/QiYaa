#include "jam/client.h"
#include "jam/codec.h"
#include "jam/session_store.h"
#include "support/jam_stub_server.h"
#include "support/silent_wss_server.h"
#include "support/spec_fixtures.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QWebSocket>

#include <algorithm>
#include <vector>

namespace {

const QStringList kClientTypes = {
    QStringLiteral("hello"),
    QStringLiteral("create"),
    QStringLiteral("resume"),
    QStringLiteral("playing"),
    QStringLiteral("started"),
    QStringLiteral("add"),
    QStringLiteral("pin"),
    QStringLiteral("remove"),
    QStringLiteral("kick"),
    QStringLiteral("settings"),
    QStringLiteral("rotateLink"),
    QStringLiteral("end"),
    QStringLiteral("searchResult"),
    QStringLiteral("validateResult"),
    QStringLiteral("join"),
    QStringLiteral("search"),
    QStringLiteral("skip"),
};

QByteArray ReadFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("cannot read %s", qUtf8Printable(path));
    }
    return file.readAll();
}

QByteArray Example(const QString& name) {
    return ReadFile(Tests::SpecPath(QStringLiteral("jam/protocol/examples/%1.json").arg(name)));
}

Jam::ClientOptions FastOptions(qint64* now = nullptr) {
    Jam::ClientOptions options;
    options.appVersion = QStringLiteral("test");
    options.reconnectDelaysMs = {50, 100, 200};
    if (now) {
        options.clock = [now] { return *now; };
    }
    return options;
}

Jam::Session ExampleSession() {
    Jam::Session session;
    session.roomId = QStringLiteral("7k3m9q2x");
    session.hostSecret = QStringLiteral("R0CuY0ewFywBJU_1W65a_1GZ9ERuf21kPUAYWz9HUUU");
    session.joinUrl = QStringLiteral("https://jam.example.org/j/7k3m9q2x#WDkyFgMr5iV3hKwManPvsg");
    session.snapshot =
        QJsonDocument::fromJson(Example(QStringLiteral("snapshot/ok"))).object()[QStringLiteral("data")].toObject();
    session.outbox = {QStringLiteral("i4"), QStringLiteral("i5")};
    return session;
}

}  // namespace

class JamTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void everyProtocolExampleDecodesAsTheSpecSays();
    void clientExamplesSurviveEncodingAndDecoding();
    void patternsMatchTheWholeTextAsEcmaScriptDoes_data();
    void patternsMatchTheWholeTextAsEcmaScriptDoes();
    void resumeWithoutSnapshotSendsAnExplicitNull();
    void anUnknownTypeIsNotAnError();
    void socketUrlFollowsTheServerAddress();
    void saysHelloAndGoesOnlineWithTheClockOffset();
    void helloCarriesAVersionTheServerTakes();
    void reconnectsAfterTheDelaysAndAtOnceWhenTheNetworkIsBack();
    void aServerThatIsDownIsOfflineAndTriedAgain();
    void aSilentConnectionOverTlsIsDroppedAndTriedAgain();
    void startedWaitsInTheOutboxUntilItCanBeSent();
    void endedStopsTheClientForGood();
    void neverSendsWhatTheServerWouldRefuse();
    void invalidMessagesAreSkippedAndUnknownReasonsArrive();
    void sessionSurvivesARestartOfTheApp();
    void aBrokenOrForeignSessionFileIsNoSession();
};

void JamTest::everyProtocolExampleDecodesAsTheSpecSays() {
    const QDir examples(Tests::SpecPath(QStringLiteral("jam/protocol/examples")));
    int checked = 0;
    for (const QString& type : examples.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QDir folder(examples.filePath(type));
        for (const QString& name : folder.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
            const QByteArray text = ReadFile(folder.filePath(name));
            const bool client = kClientTypes.contains(type);
            const QString problem = client ? Jam::DecodeClient(text).problem : Jam::DecodeServer(text).problem;
            const bool valid = client ? Jam::DecodeClient(text).isValid() : Jam::DecodeServer(text).isValid();
            const QString label = type + QLatin1Char('/') + name;
            if (name.startsWith(QLatin1String("invalid-"))) {
                QVERIFY2(!valid, qPrintable(label + QStringLiteral(" was accepted")));
            } else {
                QVERIFY2(valid, qPrintable(label + QStringLiteral(": ") + problem));
            }
            ++checked;
        }
    }
    QVERIFY2(checked > 90, qPrintable(QStringLiteral("only %1 examples found").arg(checked)));
}

void JamTest::clientExamplesSurviveEncodingAndDecoding() {
    const QDir examples(Tests::SpecPath(QStringLiteral("jam/protocol/examples")));
    for (const QString& type : kClientTypes) {
        const QDir folder(examples.filePath(type));
        for (const QString& name : folder.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
            if (name.startsWith(QLatin1String("invalid-"))) {
                continue;
            }
            const Jam::Decoded<Jam::ClientMessage> first = Jam::DecodeClient(ReadFile(folder.filePath(name)));
            QVERIFY2(first.isValid(), qPrintable(type + QLatin1Char('/') + name));
            const Jam::Decoded<Jam::ClientMessage> again = Jam::DecodeClient(Jam::Encode(*first.message));
            QVERIFY2(again.isValid(), qPrintable(again.problem));
            QVERIFY2(*again.message == *first.message, qPrintable(type + QLatin1Char('/') + name));
        }
    }
}

void JamTest::patternsMatchTheWholeTextAsEcmaScriptDoes_data() {
    QTest::addColumn<QByteArray>("text");
    QTest::addColumn<bool>("client");
    QTest::addColumn<bool>("valid");
    auto row = [](const char* name, const QJsonObject& message, bool client, bool valid) {
        QTest::newRow(name) << QJsonDocument(message).toJson(QJsonDocument::Compact) << client << valid;
    };
    auto with = [](const QString& example, const QString& field, const QString& value) {
        QJsonObject message = QJsonDocument::fromJson(Example(example)).object();
        message[field] = value;
        return message;
    };
    auto ending = [&with](const QString& example, const QString& field, const QString& end) {
        const QString value = QJsonDocument::fromJson(Example(example)).object()[field].toString();
        return with(example, field, value + end);
    };
    const QString create = QStringLiteral("create/ok");
    const QString hostName = QStringLiteral("hostName");
    const QString search = QStringLiteral("search/ok");
    const QString text = QStringLiteral("text");
    const QString lineFeed = QStringLiteral("\n");
    const QString noBreak(QChar(0x00A0));
    const QJsonObject innerNoBreak = with(create, hostName, QStringLiteral("Ма") + noBreak + QStringLiteral("ша"));
    const QJsonObject nextLine = ending(create, hostName, QString(QChar(0x0085)));
    const QJsonObject itemId = ending(QStringLiteral("started/ok"), QStringLiteral("itemId"), lineFeed);
    const QJsonObject joinUrl = ending(QStringLiteral("created/ok"), QStringLiteral("joinUrl"), lineFeed);
    const QJsonObject listenUrl = ending(QStringLiteral("playing/item-listen"), QStringLiteral("listenUrl"), lineFeed);
    row("a name ending in U+00A0", ending(create, hostName, noBreak), true, false);
    row("a name starting with U+3000", with(create, hostName, QChar(0x3000) + QStringLiteral("Маша")), true, false);
    row("a name ending in U+FEFF", ending(create, hostName, QString(QChar(0xFEFF))), true, false);
    row("U+00A0 inside a name", innerNoBreak, true, true);
    row("a name ending in U+0085, no white space to ECMAScript", nextLine, true, true);
    row("an item id ending in a line feed", itemId, true, false);
    row("a join link ending in a line feed", joinUrl, false, false);
    row("a listen link ending in a line feed", listenUrl, true, false);
    row("a search text with a control character", ending(search, text, QString(QChar(0x0001))), true, false);
    row("a search text with spaces at its ends", with(search, text, QStringLiteral(" кино ")), true, true);
    QJsonObject add = QJsonDocument::fromJson(Example(QStringLiteral("add/track"))).object();
    QJsonObject track = add[QStringLiteral("track")].toObject();
    track[QStringLiteral("coverUri")] = track[QStringLiteral("coverUri")].toString() + noBreak;
    add[QStringLiteral("track")] = track;
    row("a cover template ending in U+00A0", add, true, false);
}

void JamTest::patternsMatchTheWholeTextAsEcmaScriptDoes() {
    QFETCH(QByteArray, text);
    QFETCH(bool, client);
    QFETCH(bool, valid);
    QCOMPARE(client ? Jam::DecodeClient(text).isValid() : Jam::DecodeServer(text).isValid(), valid);
}

void JamTest::resumeWithoutSnapshotSendsAnExplicitNull() {
    const Jam::Resume resume{
        QStringLiteral("h2"), QStringLiteral("7k3m9q2x"), QString(43, QLatin1Char('H')), std::nullopt, {}
    };
    const QJsonObject json = QJsonDocument::fromJson(Jam::Encode(resume)).object();
    QVERIFY(json.contains(QStringLiteral("snapshot")));
    QVERIFY(json.value(QStringLiteral("snapshot")).isNull());
    QCOMPARE(json.value(QStringLiteral("outbox")).toArray().size(), 0);
}

void JamTest::anUnknownTypeIsNotAnError() {
    const Jam::Decoded<Jam::ServerMessage> decoded = Jam::DecodeServer(R"({"type":"fromTheFuture"})");
    QVERIFY(decoded.unknownType);
    QVERIFY(!decoded.message);
}

void JamTest::socketUrlFollowsTheServerAddress() {
    QCOMPARE(
        Jam::Client::SocketUrl(QStringLiteral("https://jam.example.org/")),
        QUrl(QStringLiteral("wss://jam.example.org/ws"))
    );
    QCOMPARE(
        Jam::Client::SocketUrl(QStringLiteral("http://localhost:8090")), QUrl(QStringLiteral("ws://localhost:8090/ws"))
    );
    QCOMPARE(
        Jam::Client::SocketUrl(QStringLiteral("jam.example.org")), QUrl(QStringLiteral("wss://jam.example.org/ws"))
    );
}

void JamTest::saysHelloAndGoesOnlineWithTheClockOffset() {
    Tests::JamStubServer server;
    qint64 now = 1'000'000;
    Jam::Client client(FastOptions(&now));
    QSignalSpy welcomed(&client, &Jam::Client::welcomed);
    client.start(server.url());
    QTRY_COMPARE(server.count(), 1);
    QTRY_COMPARE(server.received().size(), 1);
    const std::vector<Jam::ClientMessage> hello = server.messages();
    QVERIFY(hello.front() == Jam::ClientMessage(Jam::Hello{Jam::kProtocol, Jam::App::Desktop, QStringLiteral("test")}));
    server.welcome(now + 1'500);
    QTRY_COMPARE(welcomed.count(), 1);
    QCOMPARE(client.status(), Jam::Status::Online);
    QCOMPARE(client.clockOffsetMs(), 1'500);
    QCOMPARE(client.serverNow(), now + 1'500);
}

void JamTest::helloCarriesAVersionTheServerTakes() {
    const std::pair<QString, QString> versions[] = {
        {QString(), QStringLiteral("unknown")},
        {QStringLiteral("0.9.1 (dev) é"), QStringLiteral("0.9.1dev")},
        {QString(40, u'1'), QString(32, u'1')},
    };
    for (const auto& [given, sent] : versions) {
        Tests::JamStubServer server;
        Jam::ClientOptions options = FastOptions();
        options.appVersion = given;
        Jam::Client client(options);
        client.start(server.url());
        QTRY_COMPARE(server.received().size(), 1);
        QVERIFY(Jam::DecodeClient(server.received().front().toUtf8()).isValid());
        QCOMPARE(std::get<Jam::Hello>(server.messages().front()).appVersion, sent);
    }
}

void JamTest::reconnectsAfterTheDelaysAndAtOnceWhenTheNetworkIsBack() {
    Tests::JamStubServer server;
    QElapsedTimer offlineTime;
    qint64 reconnectDelayMs = -1;
    Jam::Client client(FastOptions());
    connect(&client, &Jam::Client::statusChanged, &client, [&](Jam::Status status) {
        if (status == Jam::Status::Offline) {
            offlineTime.start();
            reconnectDelayMs = -1;
        } else if (status == Jam::Status::Connecting && offlineTime.isValid()) {
            reconnectDelayMs = offlineTime.elapsed();
        }
    });
    client.start(server.url());
    QTRY_COMPARE(server.count(), 1);
    server.last().close();
    QTRY_COMPARE(client.status(), Jam::Status::Offline);
    QTRY_COMPARE(server.count(), 2);
    server.last().close();
    QTRY_COMPARE(client.status(), Jam::Status::Offline);
    QTRY_COMPARE(server.count(), 3);
    QVERIFY2(reconnectDelayMs >= 60, qPrintable(QStringLiteral("reconnected after %1 ms").arg(reconnectDelayMs)));
    QTRY_COMPARE(server.received().size(), 1);
    server.welcome();
    QTRY_COMPARE(client.status(), Jam::Status::Online);
    server.last().close();
    QTRY_COMPARE(client.status(), Jam::Status::Offline);
    client.networkBack();
    QCOMPARE(client.status(), Jam::Status::Connecting);
    QTRY_COMPARE(server.count(), 4);
}

void JamTest::aServerThatIsDownIsOfflineAndTriedAgain() {
    Jam::Client client(FastOptions());
    QSignalSpy statuses(&client, &Jam::Client::statusChanged);
    client.start(QUrl(QStringLiteral("ws://127.0.0.1:1/ws")));
    QTRY_COMPARE(client.status(), Jam::Status::Offline);
    QTRY_VERIFY(statuses.count() >= 3);
    QCOMPARE(statuses.at(2).at(0).value<Jam::Status>(), Jam::Status::Connecting);
}

void JamTest::aSilentConnectionOverTlsIsDroppedAndTriedAgain() {
    if (!QSslSocket::supportsSsl()) {
        QSKIP("no TLS backend");
    }
    using Answers = Tests::SilentWssServer::Answers;
    for (const Answers answers : {Answers::Nothing, Answers::TheUpgradeOnly}) {
        Tests::SilentWssServer server(answers);
        Jam::ClientOptions options = FastOptions();
        options.pingIntervalMs = 200;
        Jam::Client client(options);
        QSignalSpy statuses(&client, &Jam::Client::statusChanged);
        client.start(server.url());
        if (answers == Answers::TheUpgradeOnly) {
            QTRY_VERIFY(server.upgrades() >= 1);
        }
        QTRY_VERIFY(server.connections() >= 2);
        const auto wentOffline = std::any_of(statuses.cbegin(), statuses.cend(), [](const QList<QVariant>& arguments) {
            return arguments.at(0).value<Jam::Status>() == Jam::Status::Offline;
        });
        QVERIFY(wentOffline);
    }
}

void JamTest::startedWaitsInTheOutboxUntilItCanBeSent() {
    Tests::JamStubServer server;
    Jam::Client client(FastOptions());
    QSignalSpy outboxChanged(&client, &Jam::Client::outboxChanged);
    client.started(QStringLiteral("i4"));
    client.started(QStringLiteral("i4"));
    QCOMPARE(client.outbox(), QStringList{QStringLiteral("i4")});
    client.start(server.url());
    QTRY_COMPARE(server.received().size(), 1);
    server.welcome();
    QTRY_COMPARE(client.status(), Jam::Status::Online);
    client.started(QStringLiteral("i5"), false);
    QCOMPARE(client.outbox(), (QStringList{QStringLiteral("i4"), QStringLiteral("i5")}));
    client.started(QStringLiteral("i6"));
    QTRY_COMPARE(server.received().size(), 2);
    QVERIFY(server.messages().back() == Jam::ClientMessage(Jam::Started{QStringLiteral("i6")}));
    QCOMPARE(outboxChanged.count(), 2);
    client.clearOutbox();
    QVERIFY(client.outbox().isEmpty());
}

void JamTest::endedStopsTheClientForGood() {
    Tests::JamStubServer server;
    Jam::Client client(FastOptions());
    std::vector<Jam::ServerMessage> received;
    connect(&client, &Jam::Client::messageReceived, &client, [&received](const Jam::ServerMessage& message) {
        received.push_back(message);
    });
    client.start(server.url());
    QTRY_COMPARE(server.received().size(), 1);
    server.welcome();
    QTRY_COMPARE(client.status(), Jam::Status::Online);
    server.send(Example(QStringLiteral("ended/expired")));
    QTRY_COMPARE(client.status(), Jam::Status::Stopped);
    QCOMPARE(received.size(), static_cast<size_t>(1));
    QVERIFY(std::holds_alternative<Jam::Ended>(received.front()));
    QTest::qWait(300);
    QCOMPARE(server.count(), 1);
}

void JamTest::neverSendsWhatTheServerWouldRefuse() {
    Tests::JamStubServer server;
    Jam::Client client(FastOptions());
    QVERIFY(!client.send(Jam::End{QStringLiteral("e1")}));
    client.start(server.url());
    QTRY_COMPARE(server.received().size(), 1);
    server.welcome();
    QTRY_COMPARE(client.status(), Jam::Status::Online);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("not sending a create")));
    QVERIFY(!client.send(Jam::Create{QStringLiteral("c1"), QStringLiteral("  "), std::nullopt}));
    // HostSession trims a name with QString::trimmed(), and U+FEFF is no white space to Qt.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("not sending a create")));
    QVERIFY(!client.send(Jam::Create{QStringLiteral("c3"), QStringLiteral("Маша") + QChar(0xFEFF), std::nullopt}));
    QVERIFY(client.send(Jam::Create{QStringLiteral("c2"), QStringLiteral("Маша"), std::nullopt}));
    QTRY_COMPARE(server.received().size(), 2);
    QVERIFY(
        server.messages().back()
        == Jam::ClientMessage(Jam::Create{QStringLiteral("c2"), QStringLiteral("Маша"), std::nullopt})
    );
}

void JamTest::invalidMessagesAreSkippedAndUnknownReasonsArrive() {
    Tests::JamStubServer server;
    Jam::Client client(FastOptions());
    std::vector<Jam::ServerMessage> received;
    connect(&client, &Jam::Client::messageReceived, &client, [&received](const Jam::ServerMessage& message) {
        received.push_back(message);
    });
    client.start(server.url());
    QTRY_COMPARE(server.received().size(), 1);
    server.welcome();
    QTRY_COMPARE(client.status(), Jam::Status::Online);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("room carries a secret")));
    server.send(Example(QStringLiteral("state/invalid-host-secret")));
    server.send(Example(QStringLiteral("rejected/invalid-unknown-reason")));
    server.send(Example(QStringLiteral("state/host")));
    QTRY_COMPARE(received.size(), static_cast<size_t>(2));
    const auto* rejected = std::get_if<Jam::Rejected>(&received[0]);
    QVERIFY(rejected);
    QVERIFY(!Jam::IsKnownReason(rejected->reason));
    const auto* state = std::get_if<Jam::State>(&received[1]);
    QVERIFY(state);
    QCOMPARE(state->room.queue.size(), static_cast<size_t>(3));
    QCOMPARE(state->room.fallback.seedsVersion, 4);
}

void JamTest::sessionSurvivesARestartOfTheApp() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("deep/jam-session.json"));
    const Jam::Session session = ExampleSession();
    QVERIFY(Jam::SessionStore(path).save(session));
    const std::optional<Jam::Session> loaded = Jam::SessionStore(path).load();
    QVERIFY(loaded);
    QVERIFY(*loaded == session);
    Jam::SessionStore(path).clear();
    QVERIFY(!Jam::SessionStore(path).load());
}

void JamTest::aBrokenOrForeignSessionFileIsNoSession() {
    QVERIFY(!Jam::DecodeSession("not json"));
    QVERIFY(!Jam::DecodeSession(R"({"version":2,"roomId":"7k3m9q2x","hostSecret":"x","joinUrl":"x"})"));
    QVERIFY(!Jam::DecodeSession(R"({"version":1,"roomId":"7k3m9q2x"})"));
    const std::optional<Jam::Session> android = Jam::DecodeSession(
        R"({"version":1,"roomId":"7k3m9q2x","hostSecret":"s","joinUrl":"u","snapshot":{"format":1,"room":{}},"outbox":["i4"]})"
    );
    QVERIFY(android);
    QCOMPARE(android->outbox, QStringList{QStringLiteral("i4")});
    QVERIFY(android->snapshot);

    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("jam-session.json"));
    QFile big(path);
    QVERIFY(big.open(QIODevice::WriteOnly));
    big.write(QByteArray(Jam::kMaxSessionFileBytes + 1, ' '));
    big.close();
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("over 4194304; ignored")));
    QVERIFY(!Jam::SessionStore(path).load());
    QVERIFY(!Jam::SessionStore(directory.filePath(QStringLiteral("missing.json"))).load());
}

QTEST_MAIN(JamTest)
#include "jam_test.moc"
