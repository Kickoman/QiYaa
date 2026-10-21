#pragma once

#include "spotify/client/endpoints.h"
#include "spotify/client/session.h"
#include "spotify/client/web.h"
#include "spotify/protocol/access_point.h"
#include "spotify/protocol/bytes.h"
#include "support/fake_access_point.h"
#include "support/mock_http_server.h"

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QString>

namespace Tests {

inline const QString kSpotifyAppVersion = QStringLiteral("0.8.0");
inline const QString kSpotifyDeviceId = QString(40, u'd');

SpotifyClient::Endpoints SpotifyEndpointsOf(const MockHttpServer& server);
SpotifyProtocol::Credentials StoredSpotifyCredentials();
QByteArray BytesOf(const SpotifyProtocol::TBytes& bytes);

class SpotifyStack {
public:
    SpotifyStack();

    bool loginStored();

    MockHttpServer http;
    FakeAccessPointServer accessPoint;
    QNetworkAccessManager network;
    SpotifyClient::Session session;
    SpotifyClient::Web web;
};

}  // namespace Tests
