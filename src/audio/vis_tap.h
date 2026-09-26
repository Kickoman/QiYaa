#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <span>

namespace Audio {

struct VisReadResult {
    uint32_t frames = 0;
    uint32_t cursor = 0;
};

class VisTap {
public:
    static constexpr uint32_t kSize = 4096;  // power of two

    // Audio thread: interleaved stereo frames.
    void write(std::span<const float> stereoFrames);
    // UI thread; at most kSize samples per channel.
    void read(std::span<float> left, std::span<float> right) const;
    // UI thread.
    VisReadResult readNew(uint32_t cursor, std::span<float> stereo) const;
    uint32_t position() const { return writePosition.load(std::memory_order_acquire); }
    void clear();

private:
    std::array<float, kSize> leftSamples{};
    std::array<float, kSize> rightSamples{};
    std::atomic<uint32_t> writePosition{0};
};

}  // namespace Audio
