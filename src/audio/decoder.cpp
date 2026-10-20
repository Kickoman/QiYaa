#include "audio/decoder.h"

#include "audio/mp4_aac_backend.h"

#include <miniaudio.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace Audio {

namespace {

constexpr auto kRecheck = std::chrono::milliseconds(10);
constexpr size_t kId3HeaderBytes = 10;

enum class Container { Mp4, Flac, Wav, Other };

ma_encoding_format EncodingOf(Container container) {
    switch (container) {
        case Container::Flac: return ma_encoding_format_flac;
        case Container::Wav: return ma_encoding_format_wav;
        case Container::Mp4:
        case Container::Other: break;
    }
    return ma_encoding_format_mp3;
}

uint64_t Id3TagBytes(const char* header) {
    uint64_t size = 0;
    for (int i = 6; i < 10; ++i) {
        size = (size << 7) | (static_cast<unsigned char>(header[i]) & 0x7F);
    }
    const bool footer = (static_cast<unsigned char>(header[5]) & 0x10) != 0;
    return kId3HeaderBytes + size + (footer ? kId3HeaderBytes : 0);
}

bool IsUrgent(Decoder::Command::Kind kind) {
    switch (kind) {
        case Decoder::Command::Kind::Start:
        case Decoder::Command::Kind::Seek:
        case Decoder::Command::Kind::Stop: return true;
        case Decoder::Command::Kind::Queue:
        case Decoder::Command::Kind::ClearQueued:
        case Decoder::Command::Kind::Advanced: break;
    }
    return false;
}

}  // namespace

struct Decoder::Source {
    Source(Decoder& parent, uint64_t id, std::shared_ptr<ByteSource> source)
        : owner(parent)
        , stream(id)
        , bytes(std::move(source)) { }
    ~Source() { close(); }
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;

    Decoder& owner;
    const uint64_t stream;
    const std::shared_ptr<ByteSource> bytes;
    uint64_t offset = 0;
    bool interrupted = false;
    ma_decoder decoder{};
    bool opened = false;
    int rate = 0;
    int channels = 0;
    std::optional<int64_t> seekTo;
    int64_t nextFrame = 0;
    bool atEnd = false;

    static ma_result OnRead(ma_decoder* decoder, void* destination, size_t bytesToRead, size_t* bytesRead) {
        auto& source = *static_cast<Source*>(decoder->pUserData);
        *bytesRead = 0;
        const auto ready = [&] { return source.bytes->available() > source.offset || source.bytes->complete(); };
        if (!source.owner.waitFor(source, ready)) {
            return MA_CANCELLED;
        }
        *bytesRead = source.bytes->read(source.offset, destination, bytesToRead);
        source.offset += *bytesRead;
        return *bytesRead > 0 ? MA_SUCCESS : MA_AT_END;
    }

    static ma_result OnSeek(ma_decoder* decoder, ma_int64 delta, ma_seek_origin origin) {
        auto& source = *static_cast<Source*>(decoder->pUserData);
        ma_int64 target = 0;
        switch (origin) {
            case ma_seek_origin_start: target = delta; break;
            case ma_seek_origin_current: target = static_cast<ma_int64>(source.offset) + delta; break;
            case ma_seek_origin_end:
                if (!source.bytes->complete()) {
                    return MA_BAD_SEEK;
                }
                target = static_cast<ma_int64>(source.bytes->available()) + delta;
                break;
        }
        if (target < 0) {
            return MA_BAD_SEEK;
        }
        const auto ready = [&] {
            return source.bytes->available() >= static_cast<uint64_t>(target) || source.bytes->complete();
        };
        if (!source.owner.waitFor(source, ready)) {
            return MA_CANCELLED;
        }
        if (static_cast<uint64_t>(target) > source.bytes->available()) {
            return MA_BAD_SEEK;
        }
        source.offset = static_cast<uint64_t>(target);
        return MA_SUCCESS;
    }

    void close() {
        if (opened) {
            ma_decoder_uninit(&decoder);
            opened = false;
        }
    }

    size_t fill(uint64_t from, char* head, size_t size) {
        offset = from;
        size_t filled = 0;
        while (filled < size) {
            size_t bytesRead = 0;
            if (OnRead(&decoder, head + filled, size - filled, &bytesRead) != MA_SUCCESS) {
                break;
            }
            filled += bytesRead;
        }
        return filled;
    }

    Container sniff() {
        char head[12];
        size_t filled = fill(0, head, sizeof(head));
        Container found = Container::Other;
        if (IsMp4(head, filled)) {
            found = Container::Mp4;
        } else if (filled >= 12 && std::memcmp(head, "RIFF", 4) == 0 && std::memcmp(head + 8, "WAVE", 4) == 0) {
            found = Container::Wav;
        } else {
            if (filled >= kId3HeaderBytes && std::memcmp(head, "ID3", 3) == 0) {
                filled = fill(Id3TagBytes(head), head, 4);
            }
            if (filled >= 4 && std::memcmp(head, "fLaC", 4) == 0) {
                found = Container::Flac;
            }
        }
        offset = 0;
        return found;
    }
};

Decoder::Decoder(BlockQueue& queue, uint32_t deviceRate, uint32_t endWait)
    : blocks(queue)
    , outputRate(deviceRate)
    , endWaitFrames(endWait)
    , thread(&Decoder::run, this) { }

Decoder::~Decoder() {
    {
        std::lock_guard lock(mutex);
        shutDown = true;
        urgent = true;
    }
    wakeUp.notify_all();
    thread.join();
}

void Decoder::send(Command command) {
    {
        std::lock_guard lock(mutex);
        urgent = urgent || IsUrgent(command.kind);
        commands.push_back(std::move(command));
    }
    wakeUp.notify_all();
}

std::vector<Decoder::Event> Decoder::takeEvents() {
    std::lock_guard lock(mutex);
    return std::exchange(events, {});
}

void Decoder::wake() {
    { std::lock_guard lock(mutex); }
    wakeUp.notify_all();
}

void Decoder::post(Event event) {
    std::lock_guard lock(mutex);
    events.push_back(event);
}

template <typename Ready>
bool Decoder::waitFor(Source& source, Ready ready) {
    std::unique_lock lock(mutex);
    wakeUp.wait(lock, [&] { return urgent || ready(); });
    if (urgent) {
        source.interrupted = true;
        return false;
    }
    return true;
}

void Decoder::idle(std::optional<std::chrono::milliseconds> limit) {
    std::unique_lock lock(mutex);
    const auto commandWaiting = [&] { return !commands.empty() || shutDown; };
    if (limit) {
        wakeUp.wait_for(lock, *limit, commandWaiting);
    } else {
        wakeUp.wait(lock, commandWaiting);
    }
}

void Decoder::run() {
    for (;;) {
        std::deque<Command> batch;
        {
            std::lock_guard lock(mutex);
            if (shutDown) {
                return;
            }
            batch.swap(commands);
            urgent = false;
        }
        for (Command& command : batch) {
            apply(std::move(command));
        }
        step();
    }
}

void Decoder::apply(Command command) {
    switch (command.kind) {
        case Command::Kind::Start:
            generation = command.generation;
            chainedFrom.reset();
            queued = {};
            marker.reset();
            ended = false;
            current = std::make_unique<Source>(*this, command.stream, std::move(command.source));
            current->nextFrame = command.frame;
            if (command.frame > 0) {
                current->seekTo = command.frame;
            }
            return;
        case Command::Kind::Queue: queued = {command.stream, std::move(command.source)}; return;
        case Command::Kind::ClearQueued:
            if (queued.stream == command.stream) {
                queued = {};
            } else if (current && chainedFrom && current->stream == command.stream) {
                marker = Marker{command.stream, 0};
                current = std::move(chainedFrom);
            }
            return;
        case Command::Kind::Seek:
            if (!current) {
                return;
            }
            if (chainedFrom && chainedFrom->stream == command.stream) {
                queued = {current->stream, current->bytes};
                current = std::move(chainedFrom);
            } else if (current->stream != command.stream) {
                return;
            }
            chainedFrom.reset();
            generation = command.generation;
            current->seekTo = command.frame;
            current->atEnd = false;
            marker.reset();
            ended = false;
            return;
        case Command::Kind::Advanced:
            if (current && current->stream == command.stream) {
                chainedFrom.reset();
            }
            return;
        case Command::Kind::Stop:
            current.reset();
            chainedFrom.reset();
            queued = {};
            marker.reset();
            ended = false;
            return;
    }
}

void Decoder::step() {
    if (!current || ended) {
        return idle(std::nullopt);
    }
    if (marker) {
        if (!writeMarker()) {
            idle(kRecheck);
        }
        return;
    }
    Source& source = *current;
    if (!source.opened) {
        if (open(source)) {
            post(
                {.kind = Event::Kind::Opened, .stream = source.stream, .rate = source.rate, .channels = source.channels}
            );
        } else if (!source.interrupted) {
            post({.kind = Event::Kind::OpenFailed, .stream = source.stream});
            current.reset();
        }
        return;
    }
    if (source.interrupted) {
        if (!source.seekTo) {
            source.seekTo = source.nextFrame;
        }
        source.close();
        return;
    }
    if (source.seekTo) {
        const int64_t target = *source.seekTo;
        const ma_result result = ma_decoder_seek_to_pcm_frame(&source.decoder, static_cast<ma_uint64>(target));
        if (source.interrupted) {
            return;
        }
        source.seekTo.reset();
        source.nextFrame = target;
        source.atEnd = result != MA_SUCCESS;
        return;
    }
    if (!source.atEnd) {
        return decodeBlock(source);
    }
    endOrChain();
}

bool Decoder::open(Source& source) {
    source.interrupted = false;
    source.offset = 0;
    source.decoder.pUserData = &source;
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, kBlockChannels, outputRate);
    ma_result result = MA_NO_BACKEND;
    const Container container = source.sniff();
    if (container == Container::Mp4) {
        ma_decoding_backend_vtable* mp4Backends[] = {Mp4AacBackend()};
        config.ppCustomBackendVTables = mp4Backends;
        config.customBackendCount = 1;
        result = ma_decoder_init(&Source::OnRead, &Source::OnSeek, &source, &config, &source.decoder);
        // The mp3 decoder would find "frames" in an MP4 file it does not understand.
        if (result == MA_SUCCESS && source.decoder.pBackendVTable != Mp4AacBackend()) {
            ma_decoder_uninit(&source.decoder);
            result = MA_NO_BACKEND;
        }
    } else if (!source.interrupted) {
        config.encodingFormat = EncodingOf(container);
        result = ma_decoder_init(&Source::OnRead, &Source::OnSeek, &source, &config, &source.decoder);
        if (result != MA_SUCCESS && !source.interrupted) {
            source.offset = 0;
            config.encodingFormat = ma_encoding_format_unknown;
            result = ma_decoder_init(&Source::OnRead, &Source::OnSeek, &source, &config, &source.decoder);
        }
    }
    if (result == MA_SUCCESS && source.interrupted) {
        ma_decoder_uninit(&source.decoder);
        result = MA_CANCELLED;
    }
    if (result != MA_SUCCESS) {
        return false;
    }
    source.opened = true;
    ma_format format;
    ma_uint32 channels = 0;
    ma_uint32 rate = 0;
    if (ma_data_source_get_data_format(source.decoder.pBackend, &format, &channels, &rate, nullptr, 0) == MA_SUCCESS) {
        source.rate = static_cast<int>(rate);
        source.channels = static_cast<int>(channels);
    }
    return true;
}

void Decoder::decodeBlock(Source& source) {
    Block* block = blocks.back();
    if (!block) {
        return idle(kRecheck);
    }
    ma_uint64 framesRead = 0;
    const ma_result result =
        ma_decoder_read_pcm_frames(&source.decoder, block->samples.data(), kBlockFrames, &framesRead);
    if (source.interrupted) {
        return;
    }
    if (framesRead > 0) {
        block->generation = generation;
        block->stream = source.stream;
        block->frame = source.nextFrame;
        block->frameCount = static_cast<uint32_t>(framesRead);
        block->endOfStream = false;
        blocks.push();
        source.nextFrame += static_cast<int64_t>(framesRead);
    }
    if (result == MA_AT_END || (result != MA_SUCCESS && framesRead == 0)) {
        source.atEnd = true;
    }
}

void Decoder::endOrChain() {
    if (!chainedFrom && queued.source && queued.source->complete()) {
        auto next = std::make_unique<Source>(*this, queued.stream, std::move(queued.source));
        queued = {};
        if (open(*next)) {
            post({.kind = Event::Kind::Opened, .stream = next->stream, .rate = next->rate, .channels = next->channels});
            chainedFrom = std::move(current);
            current = std::move(next);
        } else if (next->interrupted) {
            queued = {next->stream, next->bytes};
        } else {
            post({.kind = Event::Kind::OpenFailed, .stream = next->stream});
        }
        return;
    }
    if (blocks.size() * kBlockFrames > endWaitFrames) {
        return idle(kRecheck);
    }
    marker = Marker{current->stream, current->nextFrame};
}

bool Decoder::writeMarker() {
    Block* block = blocks.back();
    if (!block) {
        return false;
    }
    block->generation = generation;
    block->stream = marker->stream;
    block->frame = marker->frame;
    block->frameCount = 0;
    block->endOfStream = true;
    blocks.push();
    marker.reset();
    ended = true;
    return true;
}

}  // namespace Audio
