// Lock-free tap of the most recent PCM for visualizations.
// The audio thread appends; the UI thread copies out the latest N samples.
// Reads may race with writes, which only makes a frame of a visualization a
// little inexact — never unsafe, because the buffer never moves.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace Audio {

class VisTap {
public:
    static constexpr uint32_t kSize = 4096;  // power of two

    // Audio thread: interleaved stereo frames.
    void write(const float* frames, uint32_t frameCount);
    // UI thread: the latest `count` frames (count <= kSize), oldest first.
    void read(float* left, float* right, uint32_t count) const;
    // UI thread: the frames written since `*cursor` (the newest `maxFrames` of
    // them if there are more; older ones are gone after kSize), interleaved
    // stereo into `stereo`. Advances `*cursor`; returns the number of frames.
    uint32_t readNew(uint32_t* cursor, float* stereo, uint32_t maxFrames) const;
    uint32_t position() const { return writePosition.load(std::memory_order_acquire); }
    void clear();

private:
    std::array<float, kSize> leftSamples{};
    std::array<float, kSize> rightSamples{};
    std::atomic<uint32_t> writePosition{0};
};

}  // namespace Audio
