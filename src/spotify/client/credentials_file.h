#pragma once

#include "spotify/protocol/access_point.h"

#include <QString>

#include <optional>

namespace SpotifyClient {

constexpr qint64 kMaxCredentialsFileBytes = 64 * 1024;

std::optional<SpotifyProtocol::Credentials> LoadCredentials(const QString& path);

bool SaveCredentials(const QString& path, const SpotifyProtocol::Credentials& credentials);

void ForgetCredentials(const QString& path);

}  // namespace SpotifyClient
