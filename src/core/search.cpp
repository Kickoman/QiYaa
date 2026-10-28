#include "core/search.h"

namespace Core {

namespace {

void Drop(QList<SearchPick>& picks, const std::optional<SearchPick>& best) {
    if (best) {
        picks.removeIf([&best](const SearchPick& pick) { return pick.id == best->id; });
    }
}

}  // namespace

bool SearchResults::isEmpty() const {
    return !bestTrack && !bestArtist && !bestAlbum && tracks.isEmpty() && artists.isEmpty() && albums.isEmpty();
}

QList<Track> SearchResults::playable() const {
    QList<Track> all;
    if (bestTrack) {
        all << *bestTrack;
    }
    return all << tracks;
}

SearchResults Shown(SearchResults results) {
    if (results.bestTrack && !results.bestTrack->available) {
        results.bestTrack.reset();
    }
    results.tracks.removeIf([&results](const Track& track) {
        return !track.available || (results.bestTrack && track.key == results.bestTrack->key);
    });
    Drop(results.artists, results.bestArtist);
    Drop(results.albums, results.bestAlbum);
    return results;
}

SourceNotice AddedNotice(const QString& title, const QList<Track>& tracks, AddPlacement placement) {
    QList<Track> added = tracks;
    added.removeIf([](const Track& track) { return !track.available; });
    const QString subject = added.size() == 1 ? added.front().title : title;
    const SourceNoticeCode code =
        placement == AddPlacement::Next ? SourceNoticeCode::AddedNext : SourceNoticeCode::AddedToEnd;
    return SourceNotice{.code = code, .subject = subject, .count = static_cast<int>(added.size())};
}

}  // namespace Core
