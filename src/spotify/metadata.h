#pragma once

#include "spotify/bytes.h"
#include "spotify/ids.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Spotify {

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

struct AudioFileRef {
    TFileId id{};
    std::uint32_t format = 0;
};

struct TrackInfo {
    TGid gid{};
    std::string name;
    std::int64_t durationMs = 0;
    std::vector<AudioFileRef> files;
    std::vector<TrackInfo> alternatives;
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

TBytes TrackMetadataRequest(const std::string& trackUri);
std::optional<TrackInfo> ParseTrackMetadata(ByteView body);
std::optional<TrackInfo> ParseTrack(ByteView track);
std::optional<PlayableFile> PickOggVorbis(const TrackInfo& track, int preferredKbps);
std::optional<StorageUrls> ParseStorageResolve(ByteView body);

}  // namespace Spotify
