#pragma once

#include "core/track.h"
#include "spotify/protocol/ids.h"
#include "spotify/protocol/metadata.h"

#include <QString>
#include <QUrl>

#include <optional>

namespace SpotifySource {

inline const QString kSourceName = QStringLiteral("spotify");

Core::Track TrackOf(const SpotifyProtocol::TrackInfo& track);
QString UriOf(const Core::TrackKey& key);
QString IdOfUri(const QString& uri);
std::optional<SpotifyProtocol::TGid> GidOf(const Core::TrackKey& key);
QString TrackUriOf(const SpotifyProtocol::TGid& gid);
QUrl CoverUrlOf(const Core::Track& track, int pixels);
QUrl WebPageOf(const Core::Track& track);

}  // namespace SpotifySource
