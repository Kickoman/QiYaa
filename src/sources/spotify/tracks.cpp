#include "sources/spotify/tracks.h"

#include "spotify/protocol/bytes.h"

#include <QStringList>

#include <algorithm>
#include <string>
#include <vector>

namespace SpotifySource {

namespace {

constexpr char kCoverHost[] = "https://i.scdn.co/image/";
constexpr char kTrackPage[] = "https://open.spotify.com/track/";
constexpr int kAnyBitrate = 320;

QString CoverRefOf(const std::vector<SpotifyProtocol::Image>& images) {
    QStringList parts;
    for (const SpotifyProtocol::Image& image : images) {
        parts
            << QStringLiteral("%1@%2").arg(QString::fromStdString(SpotifyProtocol::ToHex(image.id))).arg(image.pixels);
    }
    return parts.join(u',');
}

}  // namespace

Core::Track TrackOf(const SpotifyProtocol::TrackInfo& track) {
    Core::Track result;
    result.key = {kSourceName, QString::fromStdString(SpotifyProtocol::Base62FromGid(track.gid))};
    result.title = QString::fromStdString(track.name);
    for (const SpotifyProtocol::ArtistRef& artist : track.artists) {
        result.artists << QString::fromStdString(artist.name);
    }
    result.album = QString::fromStdString(track.album.name);
    result.durationMs = track.durationMs;
    result.year = track.album.year;
    result.cover = {CoverRefOf(track.album.covers)};
    result.available = SpotifyProtocol::PickOggVorbis(track, kAnyBitrate).has_value();
    return result;
}

QString UriOf(const Core::TrackKey& key) {
    return QStringLiteral("spotify:track:") + key.id;
}

QString IdOfUri(const QString& uri) {
    return uri.section(u':', 2, 2);
}

std::optional<SpotifyProtocol::TGid> GidOf(const Core::TrackKey& key) {
    if (key.source != kSourceName) {
        return std::nullopt;
    }
    return SpotifyProtocol::GidFromBase62(key.id.toStdString());
}

QString TrackUriOf(const SpotifyProtocol::TGid& gid) {
    return QStringLiteral("spotify:track:") + QString::fromStdString(SpotifyProtocol::Base62FromGid(gid));
}

QUrl CoverUrlOf(const Core::Track& track, int pixels) {
    if (track.key.source != kSourceName || track.cover.value.isEmpty()) {
        return {};
    }
    QString best;
    int bestPixels = 0;
    for (const QString& part : track.cover.value.split(u',')) {
        const QString id = part.section(u'@', 0, 0);
        const int size = part.section(u'@', 1, 1).toInt();
        const bool bigEnough = size >= pixels;
        const bool bestBigEnough = bestPixels >= pixels;
        const bool better = best.isEmpty() || (bigEnough && (!bestBigEnough || size < bestPixels))
            || (!bigEnough && !bestBigEnough && size > bestPixels);
        if (better) {
            best = id;
            bestPixels = size;
        }
    }
    return best.isEmpty() ? QUrl() : QUrl(QString::fromLatin1(kCoverHost) + best);
}

QUrl WebPageOf(const Core::Track& track) {
    if (track.key.source != kSourceName) {
        return {};
    }
    return QUrl(QString::fromLatin1(kTrackPage) + track.key.id);
}

}  // namespace SpotifySource
