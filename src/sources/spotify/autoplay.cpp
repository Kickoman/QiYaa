#include "sources/spotify/autoplay.h"

#include "sources/spotify/library.h"
#include "sources/spotify/tracks.h"

#include <algorithm>
#include <utility>

namespace SpotifySource {

Autoplay::Autoplay(Library* spotifyLibrary, QString contextUri, QString stationTrackUri, QString nextPage)
    : library(spotifyLibrary)
    , context(std::move(contextUri))
    , stationTrack(std::move(stationTrackUri))
    , next(std::move(nextPage)) { }

std::shared_ptr<Autoplay> Autoplay::Of(Library* library, const QString& contextUri) {
    return std::shared_ptr<Autoplay>(new Autoplay(library, contextUri, {}, {}));
}

std::shared_ptr<Autoplay> Autoplay::Station(Library* library, const QString& trackUri, const QString& nextPage) {
    return std::shared_ptr<Autoplay>(new Autoplay(library, {}, trackUri, nextPage));
}

QStringList Autoplay::RecentUris(const QList<Core::Track>& history) {
    QStringList uris;
    for (const Core::Track& track : history) {
        if (track.key.source == kSourceName && uris.size() < kRecentTracks) {
            uris.prepend(UriOf(track.key));
        }
    }
    return uris;
}

bool Autoplay::more(const Core::ContinuationRequest& request, TDone done) {
    if (!library) {
        return false;
    }
    std::shared_ptr<Autoplay> self = shared_from_this();
    auto replied = [self, done](const ContextPage& page, const QString& error) {
        if (!error.isEmpty()) {
            return done(Core::ContinuationReply{.failure = error});
        }
        self->next = page.next;
        done(Core::ContinuationReply{.tracks = page.tracks});
    };
    if (!next.isEmpty()) {
        library->nextPage(next, replied);
    } else if (!stationTrack.isEmpty()) {
        library->station(stationTrack, replied);
    } else {
        library->autoplay(context, RecentUris(request.history), replied);
    }
    return true;
}

}  // namespace SpotifySource
