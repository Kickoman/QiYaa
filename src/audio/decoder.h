#pragma once

#include "audio/blocks.h"
#include "audio/stream_buffer.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace Audio {

class Decoder {
public:
    struct Command {
        enum class Kind { Start, Queue, ClearQueued, Seek, Advanced, Stop };
        Kind kind = Kind::Stop;
        uint64_t stream = 0;
        std::shared_ptr<ByteSource> source;
        uint32_t generation = 0;
        int64_t frame = 0;

        static Command Start(uint64_t stream, std::shared_ptr<ByteSource> source, uint32_t generation, int64_t frame) {
            return {Kind::Start, stream, std::move(source), generation, frame};
        }
        static Command Queue(uint64_t stream, std::shared_ptr<ByteSource> source) {
            return {Kind::Queue, stream, std::move(source), 0, 0};
        }
        static Command ClearQueued(uint64_t stream) { return {Kind::ClearQueued, stream, nullptr, 0, 0}; }
        static Command Seek(uint64_t stream, uint32_t generation, int64_t frame) {
            return {Kind::Seek, stream, nullptr, generation, frame};
        }
        static Command Advanced(uint64_t stream) { return {Kind::Advanced, stream, nullptr, 0, 0}; }
        static Command Stop() { return {Kind::Stop, 0, nullptr, 0, 0}; }
    };
    struct Event {
        enum class Kind { Opened, OpenFailed };
        Kind kind = Kind::Opened;
        uint64_t stream = 0;
        int rate = 0;
        int channels = 0;
    };

    Decoder(BlockQueue& queue, uint32_t deviceRate, uint32_t endWait);
    ~Decoder();
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    void send(Command command);
    std::vector<Event> takeEvents();
    void wake();

private:
    struct Source;
    struct Pending {
        uint64_t stream = 0;
        std::shared_ptr<ByteSource> source;
    };
    struct Marker {
        uint64_t stream = 0;
        int64_t frame = 0;
    };

    void run();
    void apply(Command command);
    void step();
    bool open(Source& source);
    void decodeBlock(Source& source);
    void endOrChain();
    bool writeMarker();
    void post(Event event);
    void idle(std::optional<std::chrono::milliseconds> limit);
    template <typename Ready>
    bool waitFor(Source& source, Ready ready);

    BlockQueue& blocks;
    const uint32_t outputRate;
    const uint32_t endWaitFrames;

    std::mutex mutex;
    std::condition_variable wakeUp;
    std::deque<Command> commands;
    bool urgent = false;
    bool shutDown = false;
    std::vector<Event> events;

    uint32_t generation = 0;
    std::unique_ptr<Source> current;
    std::unique_ptr<Source> chainedFrom;
    Pending queued;
    std::optional<Marker> marker;
    bool ended = false;

    std::thread thread;
};

}  // namespace Audio
