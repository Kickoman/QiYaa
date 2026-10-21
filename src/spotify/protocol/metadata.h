#pragma once

#include "spotify/protocol/bytes.h"
#include "spotify/protocol/ids.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace SpotifyProtocol {

enum class AudioFormat : std::uint32_t {
    OggVorbis96 = 0,
    OggVorbis160 = 1,
    OggVorbis320 = 2,
    Mp3_256 = 3,
    Mp3_320 = 4,
    Mp3_160 = 5,
    Mp3_96 = 6,
    Mp3_160Encrypted = 7,
    Aac24 = 8,
    Aac48 = 9,
    Flac = 16,
    XheAac24 = 18,
    XheAac16 = 19,
    XheAac12 = 20,
    Flac24Bit = 22,
};

enum class ExtensionKind : std::uint32_t { ArtistV4 = 8, AlbumV4 = 9, TrackV4 = 10 };

struct AudioFileRef {
    TFileId id{};
    std::uint32_t format = 0;
};

struct Image {
    TFileId id{};
    int pixels = 0;
};

struct ArtistRef {
    TGid gid{};
    std::string name;
};

struct AlbumRef {
    TGid gid{};
    std::string name;
    int year = 0;
    std::vector<Image> covers;
};

struct TrackInfo {
    TGid gid{};
    std::string name;
    std::vector<ArtistRef> artists;
    AlbumRef album;
    std::int64_t durationMs = 0;
    std::vector<AudioFileRef> files;
    std::vector<TrackInfo> alternatives;
};

struct AlbumInfo {
    TGid gid{};
    std::string name;
    std::vector<ArtistRef> artists;
    std::vector<TGid> tracks;
};

struct TopTracks {
    std::string country;
    std::vector<TGid> tracks;
};

struct ArtistInfo {
    TGid gid{};
    std::string name;
    std::vector<TopTracks> topTracks;
};

struct EntityData {
    std::string uri;
    int status = 0;
    TBytes data;
};

struct PlayableFile {
    TGid track{};
    AudioFileRef file;
};

enum class StorageResult : std::uint32_t { Cdn = 0, Storage = 1, Restricted = 3 };

struct StorageUrls {
    StorageResult result = StorageResult::Cdn;
    std::vector<std::string> urls;
};

TBytes MetadataRequest(const std::vector<std::string>& uris, ExtensionKind kind);
std::optional<std::vector<EntityData>> ParseMetadataReply(ByteView body);
TBytes TrackMetadataRequest(const std::string& trackUri);
std::optional<TrackInfo> ParseTrackMetadata(ByteView body);
std::optional<TrackInfo> ParseTrack(ByteView track);
std::optional<AlbumInfo> ParseAlbum(ByteView album);
std::optional<ArtistInfo> ParseArtist(ByteView artist);
std::vector<TGid> TopTracksIn(const ArtistInfo& artist, const std::string& country);
std::optional<PlayableFile> PickOggVorbis(const TrackInfo& track, int preferredKbps);
std::optional<StorageUrls> ParseStorageResolve(ByteView body);

}  // namespace SpotifyProtocol
