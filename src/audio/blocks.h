#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Audio {

inline constexpr uint32_t kBlockFrames = 1024;
inline constexpr uint32_t kBlockChannels = 2;

// What the decoder hands the device callback: samples together with what they are.
struct Block {
    uint32_t generation = 0;  // of the start or seek they answer
    uint64_t stream = 0;
    int64_t frame = 0;  // of the first sample in the track, in device frames
    uint32_t frameCount = 0;  // 0..kBlockFrames; 0 is a marker only
    bool endOfStream = false;  // nothing of this stream follows in this generation
    std::array<float, kBlockFrames * kBlockChannels> samples{};
};

// Whether generation `a` is older than `b`; correct across the wrap at 2^32.
constexpr bool Older(uint32_t a, uint32_t b) {
    return static_cast<int32_t>(a - b) < 0;
}

// A single-producer, single-consumer queue of blocks, allocated once.
class BlockQueue {
public:
    explicit BlockQueue(size_t slotCount)
        : slots(std::make_unique<Block[]>(slotCount))
        , capacity(slotCount) { }

    // Producer: the free slot to fill, or nullptr when the queue is full.
    Block* back() {
        const uint64_t index = tail.load(std::memory_order_relaxed);
        if (index - head.load(std::memory_order_acquire) >= capacity) {
            return nullptr;
        }
        return &slots[index % capacity];
    }
    // Producer: hands back() to the consumer.
    void push() { tail.store(tail.load(std::memory_order_relaxed) + 1, std::memory_order_release); }

    // Consumer: the oldest block, or nullptr.
    const Block* front() const {
        const uint64_t index = head.load(std::memory_order_relaxed);
        if (index == tail.load(std::memory_order_acquire)) {
            return nullptr;
        }
        return &slots[index % capacity];
    }
    // Consumer: a number that changes whenever front() becomes another block.
    uint64_t frontIndex() const { return head.load(std::memory_order_relaxed); }
    void pop() { head.store(head.load(std::memory_order_relaxed) + 1, std::memory_order_release); }

    // Either side: blocks waiting, as of now.
    size_t size() const {
        return static_cast<size_t>(tail.load(std::memory_order_acquire) - head.load(std::memory_order_acquire));
    }

private:
    static_assert(std::atomic<uint64_t>::is_always_lock_free);

    std::unique_ptr<Block[]> slots;
    size_t capacity;
    std::atomic<uint64_t> head{0};  // written by the consumer
    std::atomic<uint64_t> tail{0};  // written by the producer
};

// What the device callback plays now. Generation 0 means nothing yet.
struct PlaybackSnapshot {
    uint32_t generation = 0;
    uint64_t stream = 0;
    int64_t frame = 0;  // the next frame to play, from the track's start
    bool ended = false;  // the stream is over: what follows in this generation is silence

    bool operator==(const PlaybackSnapshot&) const = default;
};

// One writer (the callback), any number of readers; a reader never sees a mix of two writes. A
// sequence lock without fences: the fields are atomic, stored with release and loaded with acquire,
// which ThreadSanitizer understands.
class PlaybackState {
public:
    void publish(const PlaybackSnapshot& snapshot) {
        const uint32_t start = sequence.load(std::memory_order_relaxed);
        sequence.store(start + 1, std::memory_order_relaxed);
        generation.store(snapshot.generation, std::memory_order_release);
        stream.store(snapshot.stream, std::memory_order_release);
        frame.store(snapshot.frame, std::memory_order_release);
        ended.store(snapshot.ended, std::memory_order_release);
        sequence.store(start + 2, std::memory_order_release);
    }

    PlaybackSnapshot read() const {
        for (;;) {
            const uint32_t before = sequence.load(std::memory_order_acquire);
            PlaybackSnapshot snapshot;
            snapshot.generation = generation.load(std::memory_order_acquire);
            snapshot.stream = stream.load(std::memory_order_acquire);
            snapshot.frame = frame.load(std::memory_order_acquire);
            snapshot.ended = ended.load(std::memory_order_acquire);
            if ((before & 1) == 0 && sequence.load(std::memory_order_relaxed) == before) {
                return snapshot;
            }
        }
    }

private:
    static_assert(
        std::atomic<uint32_t>::is_always_lock_free && std::atomic<uint64_t>::is_always_lock_free
        && std::atomic<int64_t>::is_always_lock_free && std::atomic<bool>::is_always_lock_free
    );

    std::atomic<uint32_t> sequence{0};
    std::atomic<uint32_t> generation{0};
    std::atomic<uint64_t> stream{0};
    std::atomic<int64_t> frame{0};
    std::atomic<bool> ended{false};
};

}  // namespace Audio
