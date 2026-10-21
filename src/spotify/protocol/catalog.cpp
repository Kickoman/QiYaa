#include "spotify/protocol/catalog.h"

#include "spotify/protocol/protobuf.h"

#include <utility>

namespace SpotifyProtocol {

namespace {

constexpr char kPlaylistPrefix[] = "spotify:playlist:";

bool StartsWith(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}

std::string ListName(ByteView attributes) {
    ProtoReader reader(attributes);
    while (reader.next()) {
        if (reader.field() == 1) {
            return reader.string();
        }
    }
    return {};
}

std::optional<CollectionItem> ParseCollectionItem(ByteView data) {
    CollectionItem item;
    bool removed = false;
    ProtoReader reader(data);
    while (reader.next()) {
        switch (reader.field()) {
            case 1: item.uri = reader.string(); break;
            case 2: item.addedAt = static_cast<std::int64_t>(reader.varint()); break;
            case 3: removed = reader.varint() != 0; break;
            default: break;
        }
    }
    if (reader.failed() || removed || item.uri.empty()) {
        return std::nullopt;
    }
    return item;
}

struct MetaItem {
    std::string name;
    int status = 0;
};

MetaItem ParseMetaItem(ByteView data) {
    MetaItem meta;
    ProtoReader reader(data);
    while (reader.next()) {
        if (reader.field() == 2) {
            meta.name = ListName(reader.bytes());
        } else if (reader.field() == 9) {
            meta.status = static_cast<int>(reader.zigzag());
        }
    }
    return meta;
}

}  // namespace

TBytes
CollectionPageRequest(const std::string& username, const std::string& set, const std::string& pageToken, int limit) {
    ProtoWriter request;
    request.string(1, username).string(2, set);
    if (!pageToken.empty()) {
        request.string(3, pageToken);
    }
    request.varint(4, static_cast<std::uint64_t>(limit));
    return request.data();
}

std::optional<CollectionPage> ParseCollectionPage(ByteView body) {
    CollectionPage page;
    ProtoReader reader(body);
    while (reader.next()) {
        if (reader.field() == 1) {
            if (std::optional<CollectionItem> item = ParseCollectionItem(reader.bytes())) {
                page.items.push_back(std::move(*item));
            }
        } else if (reader.field() == 2) {
            page.nextPageToken = reader.string();
        }
    }
    if (reader.failed()) {
        return std::nullopt;
    }
    return page;
}

std::optional<ListPage> ParseListPage(ByteView body) {
    ListPage page;
    std::vector<MetaItem> metas;
    ProtoReader reader(body);
    while (reader.next()) {
        switch (reader.field()) {
            case 2: page.length = static_cast<int>(reader.varint()); break;
            case 3: page.name = ListName(reader.bytes()); break;
            case 5: {
                ProtoReader contents(reader.bytes());
                while (contents.next()) {
                    switch (contents.field()) {
                        case 1: page.from = static_cast<int>(contents.varint()); break;
                        case 2: page.truncated = contents.varint() != 0; break;
                        case 3: {
                            ListItem item;
                            ProtoReader entry(contents.bytes());
                            while (entry.next()) {
                                if (entry.field() == 1) {
                                    item.uri = entry.string();
                                }
                            }
                            page.items.push_back(std::move(item));
                            break;
                        }
                        case 4: metas.push_back(ParseMetaItem(contents.bytes())); break;
                        default: break;
                    }
                }
                if (contents.failed()) {
                    return std::nullopt;
                }
                break;
            }
            default: break;
        }
    }
    if (reader.failed()) {
        return std::nullopt;
    }
    if (metas.size() == page.items.size()) {
        std::vector<ListItem> kept;
        for (std::size_t i = 0; i < metas.size(); ++i) {
            if (metas[i].status != 0 && metas[i].status != 200) {
                continue;
            }
            page.items[i].name = metas[i].name;
            kept.push_back(std::move(page.items[i]));
        }
        page.items = std::move(kept);
    }
    return page;
}

std::vector<ListItem> PlaylistsOf(const ListPage& rootlist) {
    std::vector<ListItem> playlists;
    for (const ListItem& item : rootlist.items) {
        if (StartsWith(item.uri, kPlaylistPrefix)) {
            playlists.push_back(item);
        }
    }
    return playlists;
}

TBytes AutoplayRequest(const std::string& contextUri, const std::vector<std::string>& recentTrackUris) {
    ProtoWriter request;
    request.string(1, contextUri);
    for (const std::string& uri : recentTrackUris) {
        request.string(2, uri);
    }
    return request.data();
}

}  // namespace SpotifyProtocol
