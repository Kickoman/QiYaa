// Lock-free tap of the most recent PCM for visualizations.
// The audio thread appends; the UI thread copies out the latest N samples.
// Reads may race with writes, which only makes a frame of a visualization a
// little inexact — never unsafe, because the buffer never moves.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

namespace qiyaa::audio {

class VisTap {
public:
    static constexpr uint32_t kSize = 4096;  // power of two

    // Audio thread: interleaved stereo frames.
    void write(const float* frames, uint32_t frameCount) {
        uint32_t pos = m_pos.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < frameCount; ++i, ++pos) {
            const uint32_t k = pos & (kSize - 1);
            m_left[k] = frames[i * 2];
            m_right[k] = frames[i * 2 + 1];
        }
        m_pos.store(pos, std::memory_order_release);
    }

    // UI thread: the latest `count` frames (count <= kSize), oldest first.
    void read(float* left, float* right, uint32_t count) const {
        const uint32_t end = m_pos.load(std::memory_order_acquire);
        const uint32_t start = end - count;
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t k = (start + i) & (kSize - 1);
            left[i] = m_left[k];
            right[i] = m_right[k];
        }
    }

    // UI thread: the frames written since `*cursor` (the newest `maxFrames` of
    // them if there are more; older ones are gone after kSize), interleaved
    // stereo into `stereo`. Advances `*cursor`; returns the number of frames.
    uint32_t readNew(uint32_t* cursor, float* stereo, uint32_t maxFrames) const {
        const uint32_t end = m_pos.load(std::memory_order_acquire);
        uint32_t n = std::min(end - *cursor, kSize);  // unsigned difference survives wrap-around
        n = std::min(n, maxFrames);
        const uint32_t start = end - n;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t k = (start + i) & (kSize - 1);
            stereo[i * 2] = m_left[k];
            stereo[i * 2 + 1] = m_right[k];
        }
        *cursor = end;
        return n;
    }
    uint32_t position() const { return m_pos.load(std::memory_order_acquire); }

    void clear() {
        m_left.fill(0);
        m_right.fill(0);
    }

private:
    std::array<float, kSize> m_left{};
    std::array<float, kSize> m_right{};
    std::atomic<uint32_t> m_pos{0};
};

}  // namespace qiyaa::audio
