#include "spotify/protocol/metadata.h"

#include "spotify/protocol/protobuf.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <utility>

namespace SpotifyProtocol {

namespace {

constexpr char kWorldwide[] = "ZZ";

int Kbps(std::uint32_t format) {
    switch (static_cast<AudioFormat>(format)) {
        case AudioFormat::OggVorbis96: return 96;
        case AudioFormat::OggVorbis160: return 160;
        case AudioFormat::OggVorbis320: return 320;
        default: return 0;
    }
}

int PixelsOfSize(std::uint64_t size) {
    switch (size) {
        case 1: return 64;
        case 2: return 640;
        case 3: return 1280;
        default: return 300;
    }
}

template <std::size_t N>
bool CopyId(ByteView bytes, std::array<std::uint8_t, N>& id) {
    if (bytes.size != N) {
        return false;
    }
    std::copy_n(bytes.data, N, id.begin());
    return true;
}

std::optional<Image> ParseImage(ByteView data) {
    Image image;
    bool haveId = false;
    std::uint64_t size = 0;
    ProtoReader reader(data);
    while (reader.next()) {
        switch (reader.field()) {
            case 1: haveId = CopyId(reader.bytes(), image.id); break;
            case 2: size = reader.varint(); break;
            case 3: image.pixels = static_cast<int>(reader.zigzag()); break;
            default: break;
        }
    }
    if (reader.failed() || !haveId) {
        return std::nullopt;
    }
    if (image.pixels <= 0) {
        image.pixels = PixelsOfSize(size);
    }
    return image;
}

void ParseImageGroup(ByteView data, std::vector<Image>& images) {
    ProtoReader reader(data);
    while (reader.next()) {
        if (reader.field() != 1) {
            continue;
        }
        if (std::optional<Image> image = ParseImage(reader.bytes())) {
            images.push_back(*image);
        }
    }
}

ArtistRef ParseArtistRef(ByteView data) {
    ArtistRef artist;
    ProtoReader reader(data);
    while (reader.next()) {
        if (reader.field() == 1) {
            CopyId(reader.bytes(), artist.gid);
        } else if (reader.field() == 2) {
            artist.name = reader.string();
        }
    }
    return artist;
}

int YearOf(ByteView date) {
    ProtoReader reader(date);
    while (reader.next()) {
        if (reader.field() == 1) {
            return static_cast<int>(reader.zigzag());
        }
    }
    return 0;
}

AlbumRef ParseAlbumRef(ByteView data) {
    AlbumRef album;
    ProtoReader reader(data);
    while (reader.next()) {
        switch (reader.field()) {
            case 1: CopyId(reader.bytes(), album.gid); break;
            case 2: album.name = reader.string(); break;
            case 6: album.year = YearOf(reader.bytes()); break;
            case 17: ParseImageGroup(reader.bytes(), album.covers); break;
            default: break;
        }
    }
    return album;
}

TGid GidOf(ByteView data) {
    TGid gid{};
    ProtoReader reader(data);
    while (reader.next()) {
        if (reader.field() == 1) {
            CopyId(reader.bytes(), gid);
        }
    }
    return gid;
}

std::optional<AudioFileRef> ParseAudioFile(ByteView data) {
    AudioFileRef file;
    bool haveId = false;
    ProtoReader reader(data);
    while (reader.next()) {
        if (reader.field() == 1 && reader.bytes().size == file.id.size()) {
            std::copy_n(reader.bytes().data, file.id.size(), file.id.begin());
            haveId = true;
        } else if (reader.field() == 2) {
            file.format = static_cast<std::uint32_t>(reader.varint());
        }
    }
    if (reader.failed() || !haveId) {
        return std::nullopt;
    }
    return file;
}

}  // namespace

TBytes MetadataRequest(const std::vector<std::string>& uris, ExtensionKind kind) {
    ProtoWriter query;
    query.varint(1, static_cast<std::uint64_t>(kind));
    ProtoWriter request;
    for (const std::string& uri : uris) {
        ProtoWriter entity;
        entity.string(1, uri).message(2, query);
        request.message(2, entity);
    }
    return request.data();
}

std::optional<std::vector<EntityData>> ParseMetadataReply(ByteView body) {
    std::vector<EntityData> entities;
    ProtoReader response(body);
    while (response.next()) {
        if (response.field() != 2) {
            continue;
        }
        ProtoReader array(response.bytes());
        while (array.next()) {
            if (array.field() != 3) {
                continue;
            }
            EntityData entity;
            ProtoReader data(array.bytes());
            while (data.next()) {
                if (data.field() == 1) {
                    ProtoReader header(data.bytes());
                    while (header.next()) {
                        if (header.field() == 1) {
                            entity.status = static_cast<int>(header.varint());
                        }
                    }
                } else if (data.field() == 2) {
                    entity.uri = data.string();
                } else if (data.field() == 3) {
                    ProtoReader any(data.bytes());
                    while (any.next()) {
                        if (any.field() == 2) {
                            entity.data = any.bytes().copy();
                        }
                    }
                }
            }
            if (data.failed()) {
                return std::nullopt;
            }
            entities.push_back(std::move(entity));
        }
        if (array.failed()) {
            return std::nullopt;
        }
    }
    if (response.failed()) {
        return std::nullopt;
    }
    return entities;
}

TBytes TrackMetadataRequest(const std::string& trackUri) {
    return MetadataRequest({trackUri}, ExtensionKind::TrackV4);
}

std::optional<TrackInfo> ParseTrack(ByteView track) {
    TrackInfo info;
    ProtoReader reader(track);
    while (reader.next()) {
        switch (reader.field()) {
            case 1: CopyId(reader.bytes(), info.gid); break;
            case 2: info.name = reader.string(); break;
            case 3: info.album = ParseAlbumRef(reader.bytes()); break;
            case 4: info.artists.push_back(ParseArtistRef(reader.bytes())); break;
            case 7: info.durationMs = reader.zigzag(); break;
            case 12:
                if (std::optional<AudioFileRef> file = ParseAudioFile(reader.bytes())) {
                    info.files.push_back(*file);
                }
                break;
            case 13:
                if (std::optional<TrackInfo> alternative = ParseTrack(reader.bytes())) {
                    info.alternatives.push_back(std::move(*alternative));
                }
                break;
            default: break;
        }
    }
    if (reader.failed()) {
        return std::nullopt;
    }
    return info;
}

std::optional<TrackInfo> ParseTrackMetadata(ByteView body) {
    const std::optional<std::vector<EntityData>> entities = ParseMetadataReply(body);
    if (!entities || entities->empty() || entities->front().data.empty()) {
        return std::nullopt;
    }
    return ParseTrack(entities->front().data);
}

std::optional<AlbumInfo> ParseAlbum(ByteView album) {
    AlbumInfo info;
    ProtoReader reader(album);
    while (reader.next()) {
        switch (reader.field()) {
            case 1: CopyId(reader.bytes(), info.gid); break;
            case 2: info.name = reader.string(); break;
            case 3: info.artists.push_back(ParseArtistRef(reader.bytes())); break;
            case 11: {
                ProtoReader disc(reader.bytes());
                while (disc.next()) {
                    if (disc.field() == 3) {
                        info.tracks.push_back(GidOf(disc.bytes()));
                    }
                }
                if (disc.failed()) {
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
    return info;
}

std::optional<ArtistInfo> ParseArtist(ByteView artist) {
    ArtistInfo info;
    ProtoReader reader(artist);
    while (reader.next()) {
        switch (reader.field()) {
            case 1: CopyId(reader.bytes(), info.gid); break;
            case 2: info.name = reader.string(); break;
            case 4: {
                TopTracks top;
                ProtoReader entry(reader.bytes());
                while (entry.next()) {
                    if (entry.field() == 1) {
                        top.country = entry.string();
                    } else if (entry.field() == 2) {
                        top.tracks.push_back(GidOf(entry.bytes()));
                    }
                }
                if (entry.failed()) {
                    return std::nullopt;
                }
                info.topTracks.push_back(std::move(top));
                break;
            }
            default: break;
        }
    }
    if (reader.failed()) {
        return std::nullopt;
    }
    return info;
}

std::vector<TGid> TopTracksIn(const ArtistInfo& artist, const std::string& country) {
    for (const char* wanted : {country.c_str(), kWorldwide, ""}) {
        for (const TopTracks& top : artist.topTracks) {
            if (top.country == wanted && !top.tracks.empty()) {
                return top.tracks;
            }
        }
    }
    return {};
}

std::optional<PlayableFile> PickOggVorbis(const TrackInfo& track, int preferredKbps) {
    std::optional<PlayableFile> best;
    int bestDistance = 0;
    for (const AudioFileRef& file : track.files) {
        const int kbps = Kbps(file.format);
        if (kbps == 0) {
            continue;
        }
        const int distance = std::abs(kbps - preferredKbps);
        if (!best || distance < bestDistance) {
            best = PlayableFile{track.gid, file};
            bestDistance = distance;
        }
    }
    if (best) {
        return best;
    }
    for (const TrackInfo& alternative : track.alternatives) {
        if (std::optional<PlayableFile> file = PickOggVorbis(alternative, preferredKbps)) {
            return file;
        }
    }
    return std::nullopt;
}

std::optional<StorageUrls> ParseStorageResolve(ByteView body) {
    StorageUrls storage;
    ProtoReader reader(body);
    while (reader.next()) {
        if (reader.field() == 1) {
            storage.result = static_cast<StorageResult>(reader.varint());
        } else if (reader.field() == 2) {
            storage.urls.push_back(reader.string());
        }
    }
    if (reader.failed()) {
        return std::nullopt;
    }
    return storage;
}

}  // namespace SpotifyProtocol
