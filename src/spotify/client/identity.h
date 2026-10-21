#pragma once

#include "spotify/protocol/access_point.h"
#include "spotify/protocol/login.h"

#include <QByteArray>
#include <QString>

namespace SpotifyClient {

inline const QString kClientId = QStringLiteral("65b708073fc0480ea92a077233ca87bd");
inline const QString kClientVersion = QStringLiteral("1.2.52.442");
constexpr quint64 kSpotifyVersion = 124200290;

QString NewDeviceId();

SpotifyProtocol::ClientIdentity Identity(const QString& appVersion, const QString& deviceId);

SpotifyProtocol::ClientTokenParams ClientTokenParams(const QString& deviceId);

QByteArray UserAgent(const QString& appVersion);

}  // namespace SpotifyClient
