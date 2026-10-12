#include "audio/mp4_aac_backend.h"

#include <aacdecoder_lib.h>
#include <minimp4.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace Audio {

namespace {

constexpr ma_uint32 kOutputChannels = 2;
constexpr uint32_t kMoovBox = 0x6d6f'6f76;  // "moov"
constexpr unsigned kSoundHandler = 0x736f'756e;  // "soun"
constexpr unsigned kMpeg4Audio = 0x40;  // object_type_indication of ISO/IEC 14496-3
constexpr int kMaxTopLevelBoxes = 64;
constexpr unsigned kMaxAccessUnitBytes = 1u << 16;
constexpr INT kPcmCapacity = 4096 * 8;  // fdk-aac's longest frame times its most channels

uint32_t ReadBigEndian32(const unsigned char* bytes) {
    return (uint32_t{bytes[0]} << 24) | (uint32_t{bytes[1]} << 16) | (uint32_t{bytes[2]} << 8)
        | uint32_t{bytes[3]};
}

// Output frame positions are those of the decoded signal: AAC frame `n` covers
// [n * frameSize - delay, (n + 1) * frameSize - delay), and frames before 0 are dropped.
class Mp4AacDecoder {
public:
    Mp4AacDecoder(ma_read_proc readProc, ma_seek_proc seekProc, void* readSeekUserData)
        : onRead(readProc)
        , onSeek(seekProc)
        , readSeekData(readSeekUserData) { }
    ~Mp4AacDecoder() {
        if (decoder) {
            aacDecoder_Close(decoder);
        }
        if (demuxOpen) {
            MP4D_close(&demux);
        }
    }
    Mp4AacDecoder(const Mp4AacDecoder&) = delete;
    Mp4AacDecoder& operator=(const Mp4AacDecoder&) = delete;

    ma_result open();
    ma_result read(void* output, ma_uint64 frameCount, ma_uint64* framesRead);
    ma_result seek(ma_uint64 frame);
    ma_uint32 sampleRate() const { return rate; }
    ma_uint64 cursor() const { return cursorFrame; }
    ma_uint64 length() const { return lengthFrames; }

private:
    bool readAt(int64_t offset, void* buffer, size_t size);
    std::optional<int64_t> indexEnd();
    ma_result decodeNext();
    static int ReadCallback(int64_t offset, void* buffer, size_t size, void* token);

    ma_read_proc onRead;
    ma_seek_proc onSeek;
    void* readSeekData;
    int64_t byteOffset = 0;
    MP4D_demux_t demux{};
    bool demuxOpen = false;
    unsigned track = 0;
    unsigned sampleCount = 0;
    HANDLE_AACDECODER decoder = nullptr;
    std::unique_ptr<UCHAR[]> accessUnit;
    std::unique_ptr<INT_PCM[]> pcm;
    ma_uint32 rate = 0;
    ma_uint64 frameSize = 0;
    ma_uint64 delay = 0;
    ma_uint64 lengthFrames = 0;
    ma_uint64 cursorFrame = 0;
    unsigned nextSample = 0;
    ma_int64 nextDecodedPosition = 0;
    ma_int64 decodedPosition = 0;
    UINT decodeFlags = 0;
    ma_uint64 pcmFrames = 0;
    ma_uint64 pcmOffset = 0;
};

bool Mp4AacDecoder::readAt(int64_t offset, void* buffer, size_t size) {
    if (offset != byteOffset) {
        if (onSeek(readSeekData, offset, ma_seek_origin_start) != MA_SUCCESS) {
            return false;
        }
        byteOffset = offset;
    }
    auto* bytes = static_cast<char*>(buffer);
    while (size > 0) {
        size_t bytesRead = 0;
        const ma_result result = onRead(readSeekData, bytes, size, &bytesRead);
        byteOffset += static_cast<int64_t>(bytesRead);
        if (bytesRead == 0 || (result != MA_SUCCESS && bytesRead < size)) {
            return false;
        }
        bytes += bytesRead;
        size -= bytesRead;
    }
    return true;
}

// Where the index (moov) ends: minimp4 needs to read no further, so a stream plays while its
// media (mdat) is still downloading.
std::optional<int64_t> Mp4AacDecoder::indexEnd() {
    int64_t offset = 0;
    for (int box = 0; box < kMaxTopLevelBoxes; ++box) {
        unsigned char header[16];
        if (!readAt(offset, header, 8)) {
            return std::nullopt;
        }
        uint64_t size = ReadBigEndian32(header);
        uint64_t headerBytes = 8;
        if (size == 1) {
            if (!readAt(offset + 8, header + 8, 8)) {
                return std::nullopt;
            }
            size = (uint64_t{ReadBigEndian32(header + 8)} << 32) | ReadBigEndian32(header + 12);
            headerBytes = 16;
        }
        // Size 0 runs to the end of the file, so no moov can follow it.
        if (size < headerBytes
            || size > static_cast<uint64_t>(std::numeric_limits<int64_t>::max() - offset)) {
            return std::nullopt;
        }
        offset += static_cast<int64_t>(size);
        if (ReadBigEndian32(header + 4) == kMoovBox) {
            return offset;
        }
    }
    return std::nullopt;
}

int Mp4AacDecoder::ReadCallback(int64_t offset, void* buffer, size_t size, void* token) {
    return static_cast<Mp4AacDecoder*>(token)->readAt(offset, buffer, size) ? 0 : 1;
}

ma_result Mp4AacDecoder::open() {
    accessUnit.reset(new (std::nothrow) UCHAR[kMaxAccessUnitBytes]);
    pcm.reset(new (std::nothrow) INT_PCM[kPcmCapacity]);
    if (!accessUnit || !pcm) {
        return MA_OUT_OF_MEMORY;
    }
    const std::optional<int64_t> end = indexEnd();
    if (!end || !MP4D_open(&demux, &ReadCallback, this, *end)) {
        return MA_INVALID_FILE;
    }
    demuxOpen = true;
    const MP4D_track_t* audio = nullptr;
    for (unsigned i = 0; i < demux.track_count && !audio; ++i) {
        const MP4D_track_t& candidate = demux.track[i];
        if (candidate.handler_type == kSoundHandler
            && candidate.object_type_indication == kMpeg4Audio && candidate.dsi
            && candidate.dsi_bytes > 0 && candidate.sample_count > 0) {
            audio = &candidate;
            track = i;
        }
    }
    if (!audio) {
        return MA_INVALID_FILE;
    }
    sampleCount = audio->sample_count;
    decoder = aacDecoder_Open(TT_MP4_RAW, 1);
    if (!decoder) {
        return MA_OUT_OF_MEMORY;
    }
    UCHAR* config[] = {audio->dsi};
    const UINT configBytes[] = {audio->dsi_bytes};
    if (aacDecoder_ConfigRaw(decoder, config, configBytes) != AAC_DEC_OK) {
        return MA_INVALID_FILE;
    }
    // Always stereo, so the format cannot change mid-stream; no limiter, whose look-ahead
    // would add to the delay.
    aacDecoder_SetParam(decoder, AAC_PCM_MIN_OUTPUT_CHANNELS, kOutputChannels);
    aacDecoder_SetParam(decoder, AAC_PCM_MAX_OUTPUT_CHANNELS, kOutputChannels);
    aacDecoder_SetParam(decoder, AAC_PCM_LIMITER_ENABLE, 0);
    if (const ma_result result = decodeNext(); result != MA_SUCCESS) {
        return result;
    }
    pcmOffset = std::min(pcmFrames, delay);
    const ma_uint64 decodable = sampleCount * frameSize;
    lengthFrames = decodable > delay ? decodable - delay : 0;
    const uint64_t duration = (uint64_t{audio->duration_hi} << 32) | audio->duration_lo;
    if (duration > 0 && audio->timescale > 0) {
        const uint64_t seconds = duration / audio->timescale;
        const uint64_t rest = duration % audio->timescale;
        if (seconds < std::numeric_limits<uint32_t>::max()) {
            lengthFrames =
                std::min<ma_uint64>(lengthFrames, seconds * rate + rest * rate / audio->timescale);
        }
    }
    return MA_SUCCESS;
}

ma_result Mp4AacDecoder::decodeNext() {
    unsigned bytes = 0;
    unsigned timestamp = 0;
    unsigned duration = 0;
    const MP4D_file_offset_t offset =
        MP4D_frame_offset(&demux, track, nextSample, &bytes, &timestamp, &duration);
    if (bytes == 0 || bytes > kMaxAccessUnitBytes) {
        return MA_INVALID_FILE;
    }
    if (!readAt(static_cast<int64_t>(offset), accessUnit.get(), bytes)) {
        return MA_IO_ERROR;
    }
    UCHAR* input = accessUnit.get();
    const UINT inputBytes = bytes;
    UINT bytesLeft = bytes;
    if (aacDecoder_Fill(decoder, &input, &inputBytes, &bytesLeft) != AAC_DEC_OK) {
        return MA_ERROR;
    }
    const AAC_DECODER_ERROR decoded =
        aacDecoder_DecodeFrame(decoder, pcm.get(), kPcmCapacity, std::exchange(decodeFlags, 0));
    if (!IS_OUTPUT_VALID(decoded)) {
        return MA_ERROR;
    }
    const CStreamInfo* info = aacDecoder_GetStreamInfo(decoder);
    if (!info || info->numChannels != static_cast<INT>(kOutputChannels) || info->frameSize <= 0
        || info->sampleRate <= 0) {
        return MA_INVALID_DATA;
    }
    if (rate == 0) {
        rate = static_cast<ma_uint32>(info->sampleRate);
        frameSize = static_cast<ma_uint64>(info->frameSize);
        delay = info->outputDelay;
        nextDecodedPosition = -static_cast<ma_int64>(delay);
    } else if (static_cast<ma_uint32>(info->sampleRate) != rate
               || static_cast<ma_uint64>(info->frameSize) != frameSize) {
        return MA_INVALID_DATA;
    }
    decodedPosition = nextDecodedPosition;
    nextDecodedPosition += static_cast<ma_int64>(frameSize);
    pcmFrames = frameSize;
    pcmOffset = 0;
    ++nextSample;
    return MA_SUCCESS;
}

ma_result Mp4AacDecoder::read(void* output, ma_uint64 frameCount, ma_uint64* framesRead) {
    auto* samples = static_cast<INT_PCM*>(output);
    ma_uint64 done = 0;
    while (done < frameCount && cursorFrame < lengthFrames) {
        if (pcmOffset == pcmFrames) {
            if (nextSample >= sampleCount) {
                break;
            }
            if (const ma_result result = decodeNext(); result != MA_SUCCESS) {
                if (done > 0) {
                    break;
                }
                if (framesRead) {
                    *framesRead = 0;
                }
                return result;
            }
            if (const auto wanted = static_cast<ma_int64>(cursorFrame); decodedPosition < wanted) {
                pcmOffset = std::min(pcmFrames, static_cast<ma_uint64>(wanted - decodedPosition));
            }
            continue;
        }
        const ma_uint64 take =
            std::min({pcmFrames - pcmOffset, frameCount - done, lengthFrames - cursorFrame});
        if (samples) {
            std::memcpy(
                samples + done * kOutputChannels, pcm.get() + pcmOffset * kOutputChannels,
                take * kOutputChannels * sizeof(INT_PCM)
            );
        }
        pcmOffset += take;
        done += take;
        cursorFrame += take;
    }
    if (framesRead) {
        *framesRead = done;
    }
    return done == 0 && frameCount > 0 ? MA_AT_END : MA_SUCCESS;
}

ma_result Mp4AacDecoder::seek(ma_uint64 frame) {
    frame = std::min(frame, lengthFrames);
    const ma_uint64 sample = (frame + delay) / frameSize;
    // Two frames early: the decoder needs them to settle (the MDCT overlap and the SBR's delay);
    // with one, the first ~1,300 frames come out wrong.
    const ma_uint64 first = std::min<ma_uint64>(sample > 2 ? sample - 2 : 0, sampleCount);
    aacDecoder_SetParam(decoder, AAC_TPDEC_CLEAR_BUFFER, 1);
    decodeFlags = AACDEC_CLRHIST;
    nextSample = static_cast<unsigned>(first);
    nextDecodedPosition = static_cast<ma_int64>(first * frameSize) - static_cast<ma_int64>(delay);
    pcmFrames = 0;
    pcmOffset = 0;
    cursorFrame = frame;
    return MA_SUCCESS;
}

// What miniaudio holds: its base must come first, so the decoder lives behind a pointer.
struct DataSource {
    ma_data_source_base base;
    Mp4AacDecoder* decoder;
};
static_assert(std::is_standard_layout_v<DataSource>);

Mp4AacDecoder& DecoderOf(ma_data_source* source) {
    return *static_cast<DataSource*>(source)->decoder;
}

ma_result
OnReadFrames(ma_data_source* source, void* output, ma_uint64 frameCount, ma_uint64* framesRead) {
    return DecoderOf(source).read(output, frameCount, framesRead);
}

ma_result OnSeekToFrame(ma_data_source* source, ma_uint64 frame) {
    return DecoderOf(source).seek(frame);
}

ma_result OnGetDataFormat(
    ma_data_source* source,
    ma_format* format,
    ma_uint32* channels,
    ma_uint32* sampleRate,
    ma_channel* channelMap,
    size_t channelMapCapacity
) {
    if (format) {
        *format = ma_format_s16;
    }
    if (channels) {
        *channels = kOutputChannels;
    }
    if (sampleRate) {
        *sampleRate = DecoderOf(source).sampleRate();
    }
    if (channelMap) {
        ma_channel_map_init_standard(
            ma_standard_channel_map_default, channelMap, channelMapCapacity, kOutputChannels
        );
    }
    return MA_SUCCESS;
}

ma_result OnGetCursor(ma_data_source* source, ma_uint64* cursor) {
    *cursor = DecoderOf(source).cursor();
    return MA_SUCCESS;
}

ma_result OnGetLength(ma_data_source* source, ma_uint64* length) {
    *length = DecoderOf(source).length();
    return MA_SUCCESS;
}

const ma_data_source_vtable kDataSourceVtable = {
    OnReadFrames, OnSeekToFrame, OnGetDataFormat, OnGetCursor, OnGetLength, nullptr, 0
};

void Destroy(DataSource* source) {
    ma_data_source_uninit(&source->base);
    delete source->decoder;
    delete source;
}

ma_result OnInit(
    void*,
    ma_read_proc onRead,
    ma_seek_proc onSeek,
    ma_tell_proc,
    void* readSeekUserData,
    const ma_decoding_backend_config*,
    const ma_allocation_callbacks*,
    ma_data_source** backend
) {
    auto* source = new (std::nothrow) DataSource{};
    if (!source) {
        return MA_OUT_OF_MEMORY;
    }
    source->decoder = new (std::nothrow) Mp4AacDecoder(onRead, onSeek, readSeekUserData);
    if (!source->decoder) {
        delete source;
        return MA_OUT_OF_MEMORY;
    }
    ma_data_source_config config = ma_data_source_config_init();
    config.vtable = &kDataSourceVtable;
    ma_result result = ma_data_source_init(&config, &source->base);
    if (result == MA_SUCCESS) {
        result = source->decoder->open();
    }
    if (result != MA_SUCCESS) {
        Destroy(source);
        return result;
    }
    *backend = source;
    return MA_SUCCESS;
}

void OnUninit(void*, ma_data_source* backend, const ma_allocation_callbacks*) {
    Destroy(static_cast<DataSource*>(backend));
}

}  // namespace

bool IsMp4(const char* head, size_t size) {
    return size >= 8 && std::memcmp(head + 4, "ftyp", 4) == 0;
}

ma_decoding_backend_vtable* Mp4AacBackend() {
    static ma_decoding_backend_vtable vtable = {OnInit, nullptr, nullptr, nullptr, OnUninit};
    return &vtable;
}

}  // namespace Audio
