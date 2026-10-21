#pragma once

#include "spotify/protocol/bytes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace SpotifyProtocol {

inline constexpr char kCollectionContentType[] = "application/vnd.collection-v2.spotify.proto";

struct CollectionItem {
    std::string uri;
    std::int64_t addedAt = 0;
};

struct CollectionPage {
    std::vector<CollectionItem> items;
    std::string nextPageToken;
};

struct ListItem {
    std::string uri;
    std::string name;
};

struct ListPage {
    std::string name;
    int length = 0;
    int from = 0;
    bool truncated = false;
    std::vector<ListItem> items;
};

TBytes
CollectionPageRequest(const std::string& username, const std::string& set, const std::string& pageToken, int limit);
std::optional<CollectionPage> ParseCollectionPage(ByteView body);
std::optional<ListPage> ParseListPage(ByteView body);
std::vector<ListItem> PlaylistsOf(const ListPage& rootlist);
TBytes AutoplayRequest(const std::string& contextUri, const std::vector<std::string>& recentTrackUris);

}  // namespace SpotifyProtocol
