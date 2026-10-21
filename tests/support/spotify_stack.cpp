#include "support/spotify_stack.h"

#include "spotify/client/identity.h"
#include "spotify/protocol/protobuf.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QUrl>

namespace Tests {

using SpotifyProtocol::ProtoWriter;

SpotifyClient::Endpoints SpotifyEndpointsOf(const MockHttpServer& server) {
    SpotifyClient::Endpoints endpoints;
    endpoints.apresolve = server.baseUrl();
    endpoints.accounts = server.baseUrl();
    endpoints.clientToken = server.baseUrl();
    endpoints.login5 = server.baseUrl();
    endpoints.pathfinder = server.baseUrl();
    endpoints.spclientScheme = QStringLiteral("http");
    return endpoints;
}

SpotifyProtocol::Credentials StoredSpotifyCredentials() {
    return SpotifyProtocol::Credentials{
        .username = "owner",
        .type = SpotifyProtocol::AuthenticationType::StoredCredentials,
        .data = SpotifyProtocol::TBytes{1, 2, 3},
    };
}

QByteArray BytesOf(const SpotifyProtocol::TBytes& bytes) {
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
}

SpotifyStack::SpotifyStack()
    : session(&network, SpotifyEndpointsOf(http), SpotifyClient::Identity(kSpotifyAppVersion, kSpotifyDeviceId))
    , web(&network, SpotifyEndpointsOf(http), &session, kSpotifyDeviceId, kSpotifyAppVersion) {
    session.setServerKey(FakeAccessPointKey());
    session.setReconnectDelaysMs({50});
    accessPoint.key.fill(0x5a);
    const QJsonObject resolved{
        {QStringLiteral("accesspoint"), QJsonArray{accessPoint.address()}},
        {QStringLiteral("spclient"), QJsonArray{QUrl(http.baseUrl()).authority()}},
    };
    http.json("GET", QStringLiteral("/"), QJsonDocument(resolved).toJson(QJsonDocument::Compact));
    http.on("POST", QStringLiteral("/v1/clienttoken"), [](const MockRequest&) {
        ProtoWriter token;
        token.string(1, "client-token").varint(3, 1209600);
        ProtoWriter reply;
        reply.varint(1, 1).message(2, token);
        return MockResponse{200, BytesOf(reply.data())};
    });
    http.on("POST", QStringLiteral("/v3/login"), [](const MockRequest&) {
        ProtoWriter ok;
        ok.string(1, "owner").string(2, "bearer-token").varint(4, 3600);
        ProtoWriter reply;
        reply.message(1, ok);
        return MockResponse{200, BytesOf(reply.data())};
    });
}

bool SpotifyStack::loginStored() {
    session.login(StoredSpotifyCredentials());
    return QTest::qWaitFor([this] { return session.loggedIn(); }, 5000);
}

}  // namespace Tests
