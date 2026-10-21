#include "spotify/client/credentials_file.h"
#include "spotify/client/endpoints.h"
#include "spotify/client/file_download.h"
#include "spotify/client/identity.h"
#include "spotify/client/login.h"
#include "spotify/client/session.h"
#include "spotify/client/web.h"
#include "spotify/protocol/audio_file.h"
#include "spotify/protocol/ids.h"
#include "spotify/protocol/protobuf.h"
#include "support/fake_access_point.h"
#include "support/mock_http_server.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QFileDevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

#include <cstring>
#include <optional>
#include <string>

namespace {

using SpotifyProtocol::ProtoWriter;
using SpotifyProtocol::TBytes;
using Tests::MockRequest;
using Tests::MockResponse;

const QString kAppVersion = QStringLiteral("0.8.0");
const QString kDeviceId = QString(40, u'd');

QByteArray Bytes(const TBytes& bytes) {
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
}

QByteArray JsonOf(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

SpotifyClient::Endpoints EndpointsOf(const Tests::MockHttpServer& server) {
    SpotifyClient::Endpoints endpoints;
    endpoints.apresolve = server.baseUrl();
    endpoints.accounts = server.baseUrl();
    endpoints.clientToken = server.baseUrl();
    endpoints.login5 = server.baseUrl();
    endpoints.pathfinder = server.baseUrl();
    endpoints.spclientScheme = QStringLiteral("http");
    return endpoints;
}

SpotifyProtocol::Credentials Stored() {
    return SpotifyProtocol::Credentials{
        .username = "owner",
        .type = SpotifyProtocol::AuthenticationType::StoredCredentials,
        .data = TBytes{1, 2, 3},
    };
}

QByteArray TrackMetadata(std::initializer_list<std::pair<std::uint32_t, std::uint8_t>> files) {
    ProtoWriter track;
    track.bytes(1, TBytes(16, 0x01)).string(2, "Song").varint(7, 2 * 3000);
    for (const auto& [format, mark] : files) {
        ProtoWriter file;
        file.bytes(1, TBytes(20, mark)).varint(2, format);
        track.message(12, file);
    }
    ProtoWriter any;
    any.string(1, "type.googleapis.com/spotify.metadata.Track").message(2, track);
    ProtoWriter data;
    data.string(2, "spotify:track:x").message(3, any);
    ProtoWriter array;
    array.varint(2, 10).message(3, data);
    ProtoWriter response;
    response.message(2, array);
    return Bytes(response.data());
}

QByteArray StorageOf(const QStringList& urls) {
    ProtoWriter storage;
    storage.varint(1, 0);
    for (const QString& url : urls) {
        storage.string(2, url.toStdString());
    }
    return Bytes(storage.data());
}

QByteArray PlainFile() {
    QByteArray file(static_cast<qsizetype>(SpotifyProtocol::kOggHeaderSize), 'h');
    const float loudness[4] = {-8.5f, 1.0f, -9.0f, 1.05f};
    std::memcpy(file.data() + 144, loudness, sizeof loudness);
    for (int i = 0; i < 60'000; ++i) {
        file += static_cast<char>((i * 31 + 7) & 0xff);
    }
    return file;
}

MockResponse Slice(const MockRequest& request, const QByteArray& file, qsizetype truncateAfter = -1) {
    const QByteArray range = request.headers.value("range");
    qsizetype from = 0;
    if (range.startsWith("bytes=")) {
        from = range.mid(6, range.indexOf('-') - 6).toLongLong();
    }
    MockResponse response{206, file.mid(from)};
    response.truncateAfter = truncateAfter;
    return response;
}

class Stack {
public:
    Stack()
        : session(&network, EndpointsOf(http), SpotifyClient::Identity(kAppVersion, kDeviceId))
        , web(&network, EndpointsOf(http), &session, kDeviceId, kAppVersion) {
        session.setServerKey(Tests::FakeAccessPointKey());
        session.setReconnectDelaysMs({50});
        accessPoint.key.fill(0x5a);
        const QString spclient = QUrl(http.baseUrl()).authority();
        http.json(
            "GET",
            QStringLiteral("/"),
            JsonOf(
                {{QStringLiteral("accesspoint"), QJsonArray{accessPoint.address()}},
                 {QStringLiteral("spclient"), QJsonArray{spclient}}}
            )
        );
        http.on("POST", QStringLiteral("/v1/clienttoken"), [](const MockRequest&) {
            ProtoWriter token;
            token.string(1, "client-token").varint(3, 1209600);
            ProtoWriter reply;
            reply.varint(1, 1).message(2, token);
            return MockResponse{200, Bytes(reply.data())};
        });
        http.on("POST", QStringLiteral("/v3/login"), [](const MockRequest&) {
            ProtoWriter ok;
            ok.string(1, "owner").string(2, "bearer-token").varint(4, 3600);
            ProtoWriter reply;
            reply.message(1, ok);
            return MockResponse{200, Bytes(reply.data())};
        });
    }

    bool loginStored() {
        session.login(Stored());
        return QTest::qWaitFor([this] { return session.loggedIn(); }, 5000);
    }

    Tests::MockHttpServer http;
    Tests::FakeAccessPointServer accessPoint;
    QNetworkAccessManager network;
    SpotifyClient::Session session;
    SpotifyClient::Web web;
};

struct Fetched {
    bool prepared = false;
    bool completed = false;
    std::optional<SpotifyClient::DownloadFailure> failure;
    QByteArray bytes;
    bool timedOut = false;
};

Fetched Fetch(SpotifyClient::FileDownload& download, bool start = true) {
    Fetched fetched;
    QObject::connect(&download, &SpotifyClient::FileDownload::prepared, [&] {
        fetched.prepared = true;
        if (start) {
            download.start();
        }
    });
    QObject::connect(&download, &SpotifyClient::FileDownload::received, [&](const QByteArray& bytes) {
        fetched.bytes += bytes;
    });
    QObject::connect(&download, &SpotifyClient::FileDownload::completed, [&] { fetched.completed = true; });
    QObject::
        connect(&download, &SpotifyClient::FileDownload::failed, [&](const SpotifyClient::DownloadFailure& failure) {
            fetched.failure = failure;
        });
    download.prepare();
    const bool ended = QTest::qWaitFor(
        [&] { return fetched.completed || fetched.failure.has_value() || (!start && fetched.prepared); }, 10'000
    );
    fetched.timedOut = !ended;
    return fetched;
}

}  // namespace

class TestSpotifyClient : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void credentialsFileKeepsTheReusableCredentialsForTheOwnerOnly() {  // SPOT-03
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("spotify-credentials"));
        QVERIFY(SpotifyClient::SaveCredentials(path, Stored()));
        const std::optional<SpotifyProtocol::Credentials> loaded = SpotifyClient::LoadCredentials(path);
        QVERIFY(loaded);
        QCOMPARE(loaded->username, std::string("owner"));
        QCOMPARE(loaded->data, (TBytes{1, 2, 3}));
#ifndef Q_OS_WIN
        QCOMPARE(
            QFile::permissions(path) & (QFileDevice::ReadOther | QFileDevice::ReadGroup), QFileDevice::Permissions{}
        );
#endif
        SpotifyClient::ForgetCredentials(path);
        QVERIFY(!QFile::exists(path));
        QVERIFY(!SpotifyClient::LoadCredentials(path));
    }

    void aTokenIsNeverSavedAsCredentials() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("spotify-credentials"));
        QVERIFY(SpotifyClient::SaveCredentials(path, SpotifyProtocol::Credentials::FromAccessToken("BQ-token")));
        QVERIFY(!SpotifyClient::LoadCredentials(path));
    }

    void pairLoginShowsTheCodeAndPollsUntilTheToken() {  // SPOT-01
        Tests::MockHttpServer http;
        http.json(
            "POST",
            QStringLiteral("/oauth2/device/authorize"),
            JsonOf(
                {{QStringLiteral("device_code"), QStringLiteral("device")},
                 {QStringLiteral("user_code"), QStringLiteral("ABCDEF")},
                 {QStringLiteral("verification_uri"), QStringLiteral("https://spotify.com/pair")},
                 {QStringLiteral("verification_uri_complete"), QStringLiteral("https://spotify.com/pair?code=ABCDEF")},
                 {QStringLiteral("expires_in"), 600},
                 {QStringLiteral("interval"), 1}}
            )
        );
        int polls = 0;
        http.on("POST", QStringLiteral("/api/token"), [&polls](const MockRequest&) {
            if (++polls == 1) {
                return MockResponse{400, JsonOf({{QStringLiteral("error"), QStringLiteral("authorization_pending")}})};
            }
            return MockResponse{200, JsonOf({{QStringLiteral("access_token"), QStringLiteral("BQ-token")}})};
        });
        QNetworkAccessManager network;
        SpotifyClient::PairLogin login(&network, EndpointsOf(http));
        QSignalSpy code(&login, &SpotifyClient::PairLogin::codeReady);
        QSignalSpy token(&login, &SpotifyClient::PairLogin::succeeded);
        login.start();
        QTRY_COMPARE(code.count(), 1);
        QCOMPARE(code.first().at(0).toString(), QStringLiteral("ABCDEF"));
        QCOMPARE(code.first().at(1).toUrl(), QUrl(QStringLiteral("https://spotify.com/pair?code=ABCDEF")));
        QTRY_COMPARE_WITH_TIMEOUT(token.count(), 1, 5000);
        QCOMPARE(token.first().at(0).toString(), QStringLiteral("BQ-token"));
        const MockRequest* authorize = http.last(QStringLiteral("/oauth2/device/authorize"));
        QVERIFY(authorize);
        QCOMPARE(authorize->formValue(QStringLiteral("client_id")), SpotifyClient::kClientId);
        QCOMPARE(authorize->formValue(QStringLiteral("scope")), QStringLiteral("streaming"));
        const MockRequest* poll = http.last(QStringLiteral("/api/token"));
        QCOMPARE(
            poll->formValue(QStringLiteral("grant_type")),
            QStringLiteral("urn:ietf:params:oauth:grant-type:device_code")
        );
        QCOMPARE(poll->formValue(QStringLiteral("device_code")), QStringLiteral("device"));
    }

    void pairLoginEndsWhenTheUserDeclines() {  // SPOT-01
        Tests::MockHttpServer http;
        http.json(
            "POST",
            QStringLiteral("/oauth2/device/authorize"),
            JsonOf(
                {{QStringLiteral("device_code"), QStringLiteral("device")},
                 {QStringLiteral("user_code"), QStringLiteral("ABCDEF")},
                 {QStringLiteral("interval"), 1}}
            )
        );
        http.json(
            "POST",
            QStringLiteral("/api/token"),
            JsonOf({{QStringLiteral("error"), QStringLiteral("access_denied")}}),
            400
        );
        QNetworkAccessManager network;
        SpotifyClient::PairLogin login(&network, EndpointsOf(http));
        QSignalSpy failed(&login, &SpotifyClient::PairLogin::failed);
        login.start();
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("access_denied"));
    }

    void browserLoginExchangesTheCodeOfItsOwnRedirect() {  // SPOT-02
        Tests::MockHttpServer http;
        QString challenge;
        http.on("POST", QStringLiteral("/api/token"), [&challenge](const MockRequest& request) {
            const QByteArray verifier = request.formValue(QStringLiteral("code_verifier")).toLatin1();
            const QByteArray hashed =
                QCryptographicHash::hash(verifier, QCryptographicHash::Sha256)
                    .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
            if (request.formValue(QStringLiteral("code")) != QLatin1String("c1")
                || QString::fromLatin1(hashed) != challenge) {
                return MockResponse{400, JsonOf({{QStringLiteral("error"), QStringLiteral("invalid_grant")}})};
            }
            return MockResponse{200, JsonOf({{QStringLiteral("access_token"), QStringLiteral("BQ-browser")}})};
        });
        QNetworkAccessManager network;
        SpotifyClient::BrowserLogin login(&network, EndpointsOf(http));
        QSignalSpy opened(&login, &SpotifyClient::BrowserLogin::openUrl);
        QSignalSpy token(&login, &SpotifyClient::BrowserLogin::succeeded);
        login.start();
        QCOMPARE(opened.count(), 1);
        const QUrlQuery query(opened.first().at(0).toUrl());
        QCOMPARE(query.queryItemValue(QStringLiteral("client_id")), SpotifyClient::kClientId);
        QCOMPARE(query.queryItemValue(QStringLiteral("code_challenge_method")), QStringLiteral("S256"));
        challenge = query.queryItemValue(QStringLiteral("code_challenge"));
        const QUrl redirect(query.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
        QCOMPARE(redirect.host(), QStringLiteral("127.0.0.1"));
        QUrl back = redirect;
        back.setQuery(QStringLiteral("code=c1&state=") + query.queryItemValue(QStringLiteral("state")));
        QNetworkReply* page = network.get(QNetworkRequest(back));
        QTRY_VERIFY(page->isFinished());
        QCOMPARE(page->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
        page->deleteLater();
        QTRY_COMPARE(token.count(), 1);
        QCOMPARE(token.first().at(0).toString(), QStringLiteral("BQ-browser"));
    }

    void browserLoginRefusesARedirectOfAnotherRequest() {  // SPOT-02
        Tests::MockHttpServer http;
        QNetworkAccessManager network;
        SpotifyClient::BrowserLogin login(&network, EndpointsOf(http));
        QSignalSpy opened(&login, &SpotifyClient::BrowserLogin::openUrl);
        QSignalSpy failed(&login, &SpotifyClient::BrowserLogin::failed);
        login.start();
        const QUrlQuery query(opened.first().at(0).toUrl());
        QUrl back(query.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
        back.setQuery(QStringLiteral("code=c1&state=someone-else"));
        QNetworkReply* page = network.get(QNetworkRequest(back));
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(!http.last(QStringLiteral("/api/token")));
        page->deleteLater();
    }

    void sessionLogsInAndKeepsTheReusableCredentials() {  // SPOT-03, SPOT-04
        Stack stack;
        stack.accessPoint.welcomeBlob = {7, 7, 7, 7};
        QVERIFY(stack.loginStored());
        QCOMPARE(stack.session.credentials().data, (TBytes{7, 7, 7, 7}));
        QCOMPARE(stack.session.username(), QStringLiteral("owner"));
        QTRY_COMPARE(stack.session.country(), QStringLiteral("BY"));
        QTRY_COMPARE(stack.session.product(), QStringLiteral("premium"));
    }

    void aRefusedLoginIsReportedAndNotRetried() {  // SPOT-04, SPOT-05
        Stack stack;
        stack.accessPoint.loginRefusal = static_cast<int>(SpotifyProtocol::LoginError::PremiumAccountRequired);
        QSignalSpy refused(&stack.session, &SpotifyClient::Session::loginRefused);
        stack.session.login(Stored());
        QTRY_COMPARE(refused.count(), 1);
        QCOMPARE(refused.first().at(0).toInt(), static_cast<int>(SpotifyProtocol::LoginError::PremiumAccountRequired));
        QTest::qWait(200);
        QCOMPARE(stack.accessPoint.connectionCount(), 1);
        QVERIFY(!stack.session.loggedIn());
    }

    void audioKeysComeBackRefusalsAreReportedAndCodeTwoIsRetried() {  // SPOT-16, SPOT-17
        Stack stack;
        QVERIFY(stack.loginStored());
        std::optional<SpotifyClient::KeyResult> result;
        const auto ask = [&] {
            result.reset();
            stack.session.audioKey(
                SpotifyProtocol::TGid{},
                SpotifyProtocol::TFileId{},
                [&](const SpotifyClient::KeyResult& got) { result = got; }
            );
            return QTest::qWaitFor([&] { return result.has_value(); }, 5000);
        };
        QVERIFY(ask());
        QVERIFY(result->key);
        QCOMPARE((*result->key)[0], std::uint8_t{0x5a});

        stack.accessPoint.keyRefusal = [](int) { return std::optional<std::uint16_t>{1}; };
        QVERIFY(ask());
        QVERIFY(!result->key);
        QCOMPARE(result->failure, SpotifyClient::KeyFailure::Refused);

        int asked = 0;
        stack.accessPoint.keyRefusal = [&asked](int) {
            return ++asked == 1 ? std::optional<std::uint16_t>{2} : std::nullopt;
        };
        QVERIFY(ask());
        QVERIFY(result->key);
        QCOMPARE(asked, 2);

        stack.accessPoint.keyRefusal = [](int) { return std::optional<std::uint16_t>{2}; };
        QVERIFY(ask());
        QCOMPARE(result->failure, SpotifyClient::KeyFailure::Failed);
    }

    void theSessionLogsInAgainAfterTheConnectionDrops() {  // SPOT-18
        Stack stack;
        stack.accessPoint.welcomeBlob = {4, 4};
        QVERIFY(stack.loginStored());
        QSignalSpy changes(&stack.session, &SpotifyClient::Session::loggedInChanged);
        stack.accessPoint.dropConnections();
        QTRY_VERIFY(changes.count() >= 2);
        QTRY_VERIFY(stack.session.loggedIn());
        QCOMPARE(stack.accessPoint.connectionCount(), 2);
        Tests::SpotifyPacket secondLogin;
        for (const Tests::SpotifyPacket& packet : stack.accessPoint.packets) {
            if (packet.command == 0xab) {
                secondLogin = packet;
            }
        }
        const SpotifyProtocol::ByteView credentials = Tests::FieldOf(secondLogin.payload, 10);
        QCOMPARE(Tests::VarintOf(credentials, 20), std::uint64_t{1});
        QCOMPARE(Tests::FieldOf(credentials, 30).copy(), (TBytes{4, 4}));
    }

    void webAsksForItsTokensOnceAndSendsThemToSpclient() {
        Stack stack;
        QVERIFY(stack.loginStored());
        stack.http.json("GET", QStringLiteral("/thing"), QByteArray("{}"));
        int answered = 0;
        for (int i = 0; i < 2; ++i) {
            stack.web.spclient(
                SpotifyClient::WebRequest{.path = QStringLiteral("/thing"), .accept = "application/json"},
                [&](const SpotifyClient::WebReply& reply) {
                    QVERIFY(reply.ok());
                    ++answered;
                }
            );
            QTRY_COMPARE(answered, i + 1);
        }
        const MockRequest* request = stack.http.last(QStringLiteral("/thing"));
        QCOMPARE(request->headers.value("authorization"), QByteArray("Bearer bearer-token"));
        QCOMPARE(request->headers.value("client-token"), QByteArray("client-token"));
        QVERIFY(request->headers.value("user-agent").contains("QiYaa 0.8.0"));
        int clientTokens = 0;
        for (const MockRequest& recorded : stack.http.requests()) {
            clientTokens += recorded.path == QLatin1String("/v1/clienttoken") ? 1 : 0;
        }
        QCOMPARE(clientTokens, 1);
    }

    void webLeavesTheAccountOutOfTheQueryWhenAskedAndPostsPathfinderQueries() {
        Stack stack;
        QVERIFY(stack.loginStored());
        stack.http.json("GET", QStringLiteral("/context-resolve/v1/spotify:station:track:x"), QByteArray("{}"));
        stack.http.json("POST", QStringLiteral("/pathfinder/v2/query"), QByteArray("{}"));
        int answered = 0;
        const auto count = [&](const SpotifyClient::WebReply& reply) {
            QVERIFY(reply.ok());
            ++answered;
        };
        stack.web.spclient(
            SpotifyClient::WebRequest{
                .path = QStringLiteral("/context-resolve/v1/spotify:station:track:x"),
                .accept = "application/json",
                .accountQuery = false,
            },
            count
        );
        QTRY_COMPARE(answered, 1);
        const MockRequest* station = stack.http.last(QStringLiteral("/context-resolve/v1/spotify:station:track:x"));
        QVERIFY(station->query.isEmpty());
        QCOMPARE(station->headers.value("accept"), QByteArray("application/json"));
        stack.web.pathfinder(QByteArray(R"({"operationName":"home"})"), count);
        QTRY_COMPARE(answered, 2);
        const MockRequest* query = stack.http.last(QStringLiteral("/pathfinder/v2/query"));
        QCOMPARE(query->body, QByteArray(R"({"operationName":"home"})"));
        QVERIFY(query->headers.value("content-type").startsWith("application/json"));
        QCOMPARE(query->headers.value("authorization"), QByteArray("Bearer bearer-token"));
        QVERIFY(query->query.isEmpty());
    }

    void aFileIsDecryptedWithoutSpotifysHeader() {  // SPOT-13
        Stack stack;
        QVERIFY(stack.loginStored());
        QByteArray encrypted = PlainFile();
        SpotifyProtocol::TAesKey key;
        key.fill(0x5a);
        SpotifyProtocol::AudioDecryptor(key).apply(
            reinterpret_cast<std::uint8_t*>(encrypted.data()), static_cast<std::size_t>(encrypted.size()), 0
        );
        stack.http.on("POST", QStringLiteral("/extended-metadata/v0/extended-metadata"), [](const MockRequest&) {
            return MockResponse{200, TrackMetadata({{0, 0x96}, {1, 0x16}, {2, 0x32}})};
        });
        const QString file = stack.http.baseUrl() + QStringLiteral("/audio/file");
        stack.http.onPrefix("GET", QStringLiteral("/storage-resolve/"), [file](const MockRequest&) {
            return MockResponse{200, StorageOf({file})};
        });
        stack.http.on("GET", QStringLiteral("/audio/file"), [encrypted](const MockRequest& request) {
            return Slice(request, encrypted);
        });
        SpotifyClient::FileDownload download(&stack.web, &stack.session, &stack.network, SpotifyProtocol::TGid{}, 320);
        const Fetched fetched = Fetch(download);
        QVERIFY(!fetched.failure);
        QVERIFY(fetched.completed);
        QCOMPARE(download.kbps(), 320);
        QCOMPARE(fetched.bytes, PlainFile().mid(static_cast<qsizetype>(SpotifyProtocol::kOggHeaderSize)));
        QVERIFY(download.normalisation());
        QCOMPARE(download.normalisation()->trackGainDb, -8.5f);
        const MockRequest* resolve = nullptr;
        for (const MockRequest& recorded : stack.http.requests()) {
            if (recorded.path.startsWith(QLatin1String("/storage-resolve/files/audio/interactive/"))) {
                resolve = &recorded;
            }
        }
        QVERIFY(resolve);
        QVERIFY(resolve->path.endsWith(QString::fromLatin1(QByteArray(20, '\x32').toHex())));
    }

    void aBrokenCdnLinkHandsOverToTheNextAtTheSameByte() {  // SPOT-14
        Stack stack;
        QVERIFY(stack.loginStored());
        QByteArray encrypted = PlainFile();
        SpotifyProtocol::TAesKey key;
        key.fill(0x5a);
        SpotifyProtocol::AudioDecryptor(key).apply(
            reinterpret_cast<std::uint8_t*>(encrypted.data()), static_cast<std::size_t>(encrypted.size()), 0
        );
        stack.http.on("POST", QStringLiteral("/extended-metadata/v0/extended-metadata"), [](const MockRequest&) {
            return MockResponse{200, TrackMetadata({{1, 0x16}})};
        });
        const QStringList urls = {
            stack.http.baseUrl() + QStringLiteral("/audio/down"),
            stack.http.baseUrl() + QStringLiteral("/audio/cut"),
            stack.http.baseUrl() + QStringLiteral("/audio/file"),
        };
        stack.http.onPrefix("GET", QStringLiteral("/storage-resolve/"), [urls](const MockRequest&) {
            return MockResponse{200, StorageOf(urls)};
        });
        stack.http.json("GET", QStringLiteral("/audio/down"), QByteArray(), 503);
        stack.http.on("GET", QStringLiteral("/audio/cut"), [encrypted](const MockRequest& request) {
            return Slice(request, encrypted, 20'000);
        });
        stack.http.on("GET", QStringLiteral("/audio/file"), [encrypted](const MockRequest& request) {
            return Slice(request, encrypted);
        });
        SpotifyClient::FileDownload download(&stack.web, &stack.session, &stack.network, SpotifyProtocol::TGid{}, 320);
        const Fetched fetched = Fetch(download);
        QVERIFY(!fetched.failure);
        QCOMPARE(download.kbps(), 160);
        QCOMPARE(fetched.bytes, PlainFile().mid(static_cast<qsizetype>(SpotifyProtocol::kOggHeaderSize)));
        const MockRequest* resumed = stack.http.last(QStringLiteral("/audio/file"));
        QVERIFY(resumed);
        QCOMPARE(resumed->headers.value("range"), QByteArray("bytes=20000-"));
    }

    void aRefusedKeyEndsTheDownloadAsSuch() {  // SPOT-17
        Stack stack;
        QVERIFY(stack.loginStored());
        stack.accessPoint.keyRefusal = [](int) { return std::optional<std::uint16_t>{1}; };
        stack.http.on("POST", QStringLiteral("/extended-metadata/v0/extended-metadata"), [](const MockRequest&) {
            return MockResponse{200, TrackMetadata({{1, 0x16}})};
        });
        SpotifyClient::FileDownload download(&stack.web, &stack.session, &stack.network, SpotifyProtocol::TGid{}, 320);
        const Fetched fetched = Fetch(download);
        QVERIFY(fetched.failure);
        QCOMPARE(fetched.failure->kind, SpotifyClient::DownloadFailure::Kind::KeyRefused);
    }

    void aTrackWithoutOggVorbisCannotPlay() {  // SPOT-15
        Stack stack;
        QVERIFY(stack.loginStored());
        stack.http.on("POST", QStringLiteral("/extended-metadata/v0/extended-metadata"), [](const MockRequest&) {
            return MockResponse{200, TrackMetadata({{16, 0xf1}, {22, 0xf2}})};
        });
        SpotifyClient::FileDownload download(&stack.web, &stack.session, &stack.network, SpotifyProtocol::TGid{}, 320);
        const Fetched fetched = Fetch(download);
        QVERIFY(fetched.failure);
        QCOMPARE(fetched.failure->kind, SpotifyClient::DownloadFailure::Kind::Track);
    }

    void playsARealTrackWithGivenCredentials() {
        const QString path = qEnvironmentVariable("QIYAA_SPOTIFY_CREDENTIALS");
        if (path.isEmpty()) {
            QSKIP("QIYAA_SPOTIFY_CREDENTIALS is not set");
        }
        const std::optional<SpotifyProtocol::Credentials> credentials = SpotifyClient::LoadCredentials(path);
        QVERIFY(credentials);
        QNetworkAccessManager network;
        const SpotifyClient::Endpoints endpoints;
        SpotifyClient::Session session(&network, endpoints, SpotifyClient::Identity(kAppVersion, kDeviceId));
        SpotifyClient::Web web(&network, endpoints, &session, kDeviceId, kAppVersion);
        session.login(*credentials);
        QTRY_VERIFY_WITH_TIMEOUT(session.loggedIn(), 15'000);
        const QString track = qEnvironmentVariable("QIYAA_SPOTIFY_TRACK", QStringLiteral("6PCoZdjSPbRMehHviTrpB3"));
        const std::optional<SpotifyProtocol::TGid> gid = SpotifyProtocol::GidFromBase62(track.toStdString());
        QVERIFY(gid);
        SpotifyClient::FileDownload download(&web, &session, &network, *gid, 320);
        const Fetched fetched = Fetch(download);
        QVERIFY2(!fetched.failure, qPrintable(fetched.failure ? fetched.failure->text : QString()));
        QVERIFY(fetched.completed);
        QVERIFY(fetched.bytes.startsWith("OggS"));
        qInfo(
            "%s: %d kbps, %lld bytes of Ogg Vorbis, track gain %.2f dB",
            qPrintable(track),
            download.kbps(),
            static_cast<long long>(fetched.bytes.size()),
            download.normalisation() ? static_cast<double>(download.normalisation()->trackGainDb) : 0.0
        );
        const QString output = qEnvironmentVariable("QIYAA_SPOTIFY_OUTPUT");
        if (!output.isEmpty()) {
            QFile file(output);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(fetched.bytes);
        }
    }
};

QTEST_GUILESS_MAIN(TestSpotifyClient)
#include "spotify_client_test.moc"
