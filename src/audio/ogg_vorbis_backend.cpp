#include "audio/ogg_vorbis_backend.h"

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>
#include <vector>

namespace Audio {

namespace {

constexpr size_t kChunkBytes = 16 * 1024;
constexpr size_t kMaxHeaderBytes = 32 * 1024 * 1024;
constexpr size_t kPageHeaderBytes = 27;
constexpr size_t kMaxPageBytes = kPageHeaderBytes + 255 + 255 * 255;
constexpr size_t kSeekRunUpPages = 5;
constexpr ma_uint32 kMaxChannels = 8;
constexpr unsigned char kEndOfStream = 0x04;
constexpr auto kLongestTail = static_cast<int64_t>(2 * kMaxPageBytes);
constexpr auto kShortestTail = static_cast<int64_t>(kPageHeaderBytes);

struct Page {
    uint64_t offset = 0;
    uint64_t size = 0;
    int64_t granule = -1;
    bool last = false;
};

int64_t ReadLittleEndian64(const unsigned char* bytes) {
    uint64_t value = 0;
    for (int i = 7; i >= 0; --i) {
        value = (value << 8) | bytes[i];
    }
    return static_cast<int64_t>(value);
}

bool Reaches(const Page& page, ma_uint64 frame) {
    return page.granule >= 0 && static_cast<ma_uint64>(page.granule) >= frame;
}

bool IsPageStart(const unsigned char* bytes) {
    return std::memcmp(bytes, "OggS", 4) == 0 && bytes[4] == 0;
}

class OggVorbisDecoder {
public:
    OggVorbisDecoder(ma_read_proc readProc, ma_seek_proc seekProc, void* readSeekUserData)
        : onRead(readProc)
        , onSeek(seekProc)
        , readSeekData(readSeekUserData) { }
    ~OggVorbisDecoder() {
        if (vorbis) {
            stb_vorbis_close(vorbis);
        }
    }
    OggVorbisDecoder(const OggVorbisDecoder&) = delete;
    OggVorbisDecoder& operator=(const OggVorbisDecoder&) = delete;

    ma_result open();
    ma_result read(float* output, ma_uint64 frameCount, ma_uint64* framesRead);
    ma_result seek(ma_uint64 frame);
    ma_uint32 sampleRate() const { return rate; }
    ma_uint32 channelCount() const { return channels; }
    ma_uint64 cursor() const { return cursorFrame; }
    ma_uint64 length() const { return lengthFrames; }

private:
    ma_result readAt(uint64_t offset, unsigned char* buffer, size_t size, size_t& got, bool whole = true);
    ma_result feed();
    void dropInput(uint64_t offset);
    ma_result startDecoder();
    ma_result restartAt(uint64_t offset);
    ma_result decodeFrame();
    ma_result readPage(uint64_t offset, Page& page);
    ma_result indexPagesUntil(ma_uint64 frame);
    void findLength();

    ma_read_proc onRead;
    ma_seek_proc onSeek;
    void* readSeekData;
    uint64_t byteOffset = std::numeric_limits<uint64_t>::max();
    std::vector<unsigned char> input;
    size_t inputUsed = 0;
    uint64_t inputOffset = 0;
    bool inputEnded = false;
    stb_vorbis* vorbis = nullptr;
    ma_uint32 rate = 0;
    ma_uint32 channels = 0;
    uint64_t audioStart = 0;
    std::vector<Page> pages;
    bool pagesComplete = false;
    ma_uint64 lengthFrames = 0;
    ma_uint64 cursorFrame = 0;
    std::vector<float> decoded;
    ma_uint64 decodedFrames = 0;
    ma_uint64 decodedOffset = 0;
    ma_uint64 decodedPosition = 0;
};

ma_result OggVorbisDecoder::readAt(uint64_t offset, unsigned char* buffer, size_t size, size_t& got, bool whole) {
    got = 0;
    if (offset != byteOffset) {
        if (offset > static_cast<uint64_t>(std::numeric_limits<ma_int64>::max())) {
            return MA_BAD_SEEK;
        }
        if (const ma_result result = onSeek(readSeekData, static_cast<ma_int64>(offset), ma_seek_origin_start);
            result != MA_SUCCESS) {
            byteOffset = std::numeric_limits<uint64_t>::max();
            return result;
        }
        byteOffset = offset;
    }
    while (got < size) {
        size_t bytesRead = 0;
        const ma_result result = onRead(readSeekData, buffer + got, size - got, &bytesRead);
        got += bytesRead;
        byteOffset += bytesRead;
        if (result == MA_AT_END || (result == MA_SUCCESS && bytesRead == 0)) {
            break;
        }
        if (result != MA_SUCCESS) {
            return result;
        }
        if (!whole) {
            break;
        }
    }
    return MA_SUCCESS;
}

ma_result OggVorbisDecoder::feed() {
    if (inputUsed > 0) {
        input.erase(input.begin(), input.begin() + static_cast<std::ptrdiff_t>(inputUsed));
        inputOffset += inputUsed;
        inputUsed = 0;
    }
    const size_t kept = input.size();
    input.resize(kept + kChunkBytes);
    size_t got = 0;
    const ma_result result = readAt(inputOffset + kept, input.data() + kept, kChunkBytes, got, false);
    input.resize(kept + got);
    if (result != MA_SUCCESS) {
        return result;
    }
    return got == 0 ? MA_AT_END : MA_SUCCESS;
}

void OggVorbisDecoder::dropInput(uint64_t offset) {
    input.clear();
    inputUsed = 0;
    inputOffset = offset;
    inputEnded = false;
    decodedFrames = 0;
    decodedOffset = 0;
}

ma_result OggVorbisDecoder::startDecoder() {
    if (vorbis) {
        stb_vorbis_close(vorbis);
        vorbis = nullptr;
    }
    dropInput(0);
    for (;;) {
        int used = 0;
        int error = VORBIS__no_error;
        vorbis = stb_vorbis_open_pushdata(input.data(), static_cast<int>(input.size()), &used, &error, nullptr);
        if (vorbis) {
            inputUsed = static_cast<size_t>(used);
            return MA_SUCCESS;
        }
        if (error != VORBIS_need_more_data || input.size() >= kMaxHeaderBytes) {
            return MA_INVALID_FILE;
        }
        if (const ma_result result = feed(); result != MA_SUCCESS) {
            return result == MA_AT_END ? MA_INVALID_FILE : result;
        }
    }
}

ma_result OggVorbisDecoder::restartAt(uint64_t offset) {
    stb_vorbis_flush_pushdata(vorbis);
    dropInput(offset);
    return MA_SUCCESS;
}

ma_result OggVorbisDecoder::decodeFrame() {
    for (;;) {
        int frameChannels = 0;
        float** output = nullptr;
        int samples = 0;
        const int used = stb_vorbis_decode_frame_pushdata(
            vorbis,
            input.data() + inputUsed,
            static_cast<int>(input.size() - inputUsed),
            &frameChannels,
            &output,
            &samples
        );
        if (used == 0 && samples == 0) {
            if (inputEnded) {
                return MA_AT_END;
            }
            if (const ma_result result = feed(); result == MA_AT_END) {
                inputEnded = true;
            } else if (result != MA_SUCCESS) {
                return result;
            }
            continue;
        }
        inputUsed += static_cast<size_t>(used);
        const int next = stb_vorbis_get_sample_offset(vorbis);
        if (samples <= 0 || next < samples || frameChannels != static_cast<int>(channels)) {
            continue;
        }
        decoded.resize(static_cast<size_t>(samples) * channels);
        for (int frame = 0; frame < samples; ++frame) {
            for (ma_uint32 channel = 0; channel < channels; ++channel) {
                decoded[static_cast<size_t>(frame) * channels + channel] = output[channel][frame];
            }
        }
        decodedFrames = static_cast<ma_uint64>(samples);
        decodedOffset = 0;
        decodedPosition = static_cast<ma_uint64>(next - samples);
        return MA_SUCCESS;
    }
}

ma_result OggVorbisDecoder::readPage(uint64_t offset, Page& page) {
    unsigned char header[kPageHeaderBytes + 255];
    size_t got = 0;
    if (const ma_result result = readAt(offset, header, kPageHeaderBytes, got); result != MA_SUCCESS) {
        return result;
    }
    if (got < kPageHeaderBytes || !IsPageStart(header)) {
        return MA_AT_END;
    }
    const size_t segments = header[26];
    if (const ma_result result = readAt(offset + kPageHeaderBytes, header + kPageHeaderBytes, segments, got);
        result != MA_SUCCESS) {
        return result;
    }
    if (got < segments) {
        return MA_AT_END;
    }
    uint64_t body = 0;
    for (size_t i = 0; i < segments; ++i) {
        body += header[kPageHeaderBytes + i];
    }
    page.offset = offset;
    page.size = kPageHeaderBytes + segments + body;
    page.granule = ReadLittleEndian64(header + 6);
    page.last = (header[5] & kEndOfStream) != 0;
    return MA_SUCCESS;
}

ma_result OggVorbisDecoder::indexPagesUntil(ma_uint64 frame) {
    while (!pagesComplete) {
        if (!pages.empty() && Reaches(pages.back(), frame)) {
            return MA_SUCCESS;
        }
        const uint64_t offset = pages.empty() ? audioStart : pages.back().offset + pages.back().size;
        Page page;
        const ma_result result = readPage(offset, page);
        if (result == MA_AT_END || result == MA_BAD_SEEK) {
            pagesComplete = true;
            break;
        }
        if (result != MA_SUCCESS) {
            return result;
        }
        pages.push_back(page);
        pagesComplete = page.last;
    }
    return MA_SUCCESS;
}

void OggVorbisDecoder::findLength() {
    std::vector<unsigned char> tail;
    for (int64_t size = kLongestTail; size >= kShortestTail; size /= 2) {
        if (onSeek(readSeekData, -size, ma_seek_origin_end) != MA_SUCCESS) {
            continue;
        }
        byteOffset = std::numeric_limits<uint64_t>::max();
        tail.resize(static_cast<size_t>(size));
        size_t got = 0;
        while (got < tail.size()) {
            size_t bytesRead = 0;
            const ma_result result = onRead(readSeekData, tail.data() + got, tail.size() - got, &bytesRead);
            got += bytesRead;
            if (result != MA_SUCCESS || bytesRead == 0) {
                break;
            }
        }
        tail.resize(got);
        break;
    }
    for (size_t at = tail.size() >= kPageHeaderBytes ? tail.size() - kPageHeaderBytes + 1 : 0; at-- > 0;) {
        if (!IsPageStart(tail.data() + at)) {
            continue;
        }
        const int64_t granule = ReadLittleEndian64(tail.data() + at + 6);
        if (granule > 0 && granule <= INT_MAX) {
            lengthFrames = static_cast<ma_uint64>(granule);
            return;
        }
    }
}

ma_result OggVorbisDecoder::open() {
    if (const ma_result result = startDecoder(); result != MA_SUCCESS) {
        return result;
    }
    const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    if (info.channels < 1 || static_cast<ma_uint32>(info.channels) > kMaxChannels || info.sample_rate == 0) {
        return MA_INVALID_FILE;
    }
    channels = static_cast<ma_uint32>(info.channels);
    rate = info.sample_rate;
    audioStart = inputOffset + inputUsed;
    findLength();
    return MA_SUCCESS;
}

ma_result OggVorbisDecoder::read(float* output, ma_uint64 frameCount, ma_uint64* framesRead) {
    ma_uint64 done = 0;
    ma_result failure = MA_SUCCESS;
    while (done < frameCount && (lengthFrames == 0 || cursorFrame < lengthFrames)) {
        if (decodedOffset == decodedFrames) {
            const ma_result result = decodeFrame();
            if (result != MA_SUCCESS) {
                failure = result == MA_AT_END ? MA_SUCCESS : result;
                break;
            }
            if (decodedPosition < cursorFrame) {
                decodedOffset = std::min(decodedFrames, cursorFrame - decodedPosition);
            } else {
                cursorFrame = decodedPosition;
            }
            continue;
        }
        ma_uint64 take = std::min(decodedFrames - decodedOffset, frameCount - done);
        if (lengthFrames > 0) {
            take = std::min(take, lengthFrames - cursorFrame);
        }
        if (output) {
            std::memcpy(
                output + done * channels,
                decoded.data() + decodedOffset * channels,
                static_cast<size_t>(take * channels) * sizeof(float)
            );
        }
        decodedOffset += take;
        done += take;
        cursorFrame += take;
    }
    if (framesRead) {
        *framesRead = done;
    }
    if (done == 0 && failure != MA_SUCCESS) {
        return failure;
    }
    return done == 0 && frameCount > 0 ? MA_AT_END : MA_SUCCESS;
}

ma_result OggVorbisDecoder::seek(ma_uint64 frame) {
    if (lengthFrames > 0) {
        frame = std::min(frame, lengthFrames);
    }
    const ma_uint64 decodedEnd = decodedPosition + decodedFrames;
    if (decodedFrames > 0 && frame >= decodedPosition && frame <= decodedEnd) {
        decodedOffset = frame - decodedPosition;
        cursorFrame = frame;
        return MA_SUCCESS;
    }
    if (const ma_result result = indexPagesUntil(frame); result != MA_SUCCESS) {
        return result;
    }
    size_t target = 0;
    while (target < pages.size() && !Reaches(pages[target], frame)) {
        ++target;
    }
    ma_result result = MA_SUCCESS;
    if (target < kSeekRunUpPages) {
        result = startDecoder();
    } else {
        result = restartAt(pages[target - kSeekRunUpPages].offset);
    }
    if (result != MA_SUCCESS) {
        return result;
    }
    cursorFrame = frame;
    return MA_SUCCESS;
}

struct DataSource {
    ma_data_source_base base;
    OggVorbisDecoder* decoder;
};
static_assert(std::is_standard_layout_v<DataSource>);

OggVorbisDecoder& DecoderOf(ma_data_source* source) {
    return *static_cast<DataSource*>(source)->decoder;
}

ma_result OnReadFrames(ma_data_source* source, void* output, ma_uint64 frameCount, ma_uint64* framesRead) {
    return DecoderOf(source).read(static_cast<float*>(output), frameCount, framesRead);
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
    const OggVorbisDecoder& decoder = DecoderOf(source);
    if (format) {
        *format = ma_format_f32;
    }
    if (channels) {
        *channels = decoder.channelCount();
    }
    if (sampleRate) {
        *sampleRate = decoder.sampleRate();
    }
    if (channelMap) {
        ma_channel_map_init_standard(
            ma_standard_channel_map_vorbis, channelMap, channelMapCapacity, decoder.channelCount()
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

const ma_data_source_vtable kDataSourceVtable =
    {OnReadFrames, OnSeekToFrame, OnGetDataFormat, OnGetCursor, OnGetLength, nullptr, 0};

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
    source->decoder = new (std::nothrow) OggVorbisDecoder(onRead, onSeek, readSeekUserData);
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

bool IsOgg(const char* head, size_t size) {
    return size >= 4 && std::memcmp(head, "OggS", 4) == 0;
}

ma_decoding_backend_vtable* OggVorbisBackend() {
    static ma_decoding_backend_vtable vtable = {OnInit, nullptr, nullptr, nullptr, OnUninit};
    return &vtable;
}

}  // namespace Audio
