#include "spotify/metadata.h"

#include "spotify/protobuf.h"

#include <algorithm>
#include <cstdlib>

namespace Spotify {

namespace {

constexpr std::uint64_t kTrackV4 = 10;

int Kbps(std::uint32_t format) {
    switch (static_cast<AudioFormat>(format)) {
        case AudioFormat::OggVorbis96: return 96;
        case AudioFormat::OggVorbis160: return 160;
        case AudioFormat::OggVorbis320: return 320;
        default: return 0;
    }
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

TBytes TrackMetadataRequest(const std::string& trackUri) {
    ProtoWriter query;
    query.varint(1, kTrackV4);
    ProtoWriter entity;
    entity.string(1, trackUri).message(2, query);
    ProtoWriter request;
    request.message(2, entity);
    return request.data();
}

std::optional<TrackInfo> ParseTrack(ByteView track) {
    TrackInfo info;
    ProtoReader reader(track);
    while (reader.next()) {
        switch (reader.field()) {
            case 1:
                if (reader.bytes().size == info.gid.size()) {
                    std::copy_n(reader.bytes().data, info.gid.size(), info.gid.begin());
                }
                break;
            case 2: info.name = reader.string(); break;
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
            ProtoReader data(array.bytes());
            while (data.next()) {
                if (data.field() != 3) {
                    continue;
                }
                ProtoReader any(data.bytes());
                while (any.next()) {
                    if (any.field() == 2) {
                        return ParseTrack(any.bytes());
                    }
                }
            }
        }
    }
    return std::nullopt;
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

}  // namespace Spotify
