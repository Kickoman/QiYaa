#include "audio/vis_tap.h"

#include <algorithm>

namespace Audio {

void VisTap::write(const float* frames, uint32_t frameCount) {
    uint32_t pos = writePosition.load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < frameCount; ++i, ++pos) {
        const uint32_t k = pos & (kSize - 1);
        leftSamples[k] = frames[i * 2];
        rightSamples[k] = frames[i * 2 + 1];
    }
    writePosition.store(pos, std::memory_order_release);
}

void VisTap::read(float* left, float* right, uint32_t count) const {
    const uint32_t end = writePosition.load(std::memory_order_acquire);
    const uint32_t start = end - count;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t k = (start + i) & (kSize - 1);
        left[i] = leftSamples[k];
        right[i] = rightSamples[k];
    }
}

uint32_t VisTap::readNew(uint32_t* cursor, float* stereo, uint32_t maxFrames) const {
    const uint32_t end = writePosition.load(std::memory_order_acquire);
    uint32_t n = std::min(end - *cursor, kSize);  // unsigned difference survives wrap-around
    n = std::min(n, maxFrames);
    const uint32_t start = end - n;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t k = (start + i) & (kSize - 1);
        stereo[i * 2] = leftSamples[k];
        stereo[i * 2 + 1] = rightSamples[k];
    }
    *cursor = end;
    return n;
}

void VisTap::clear() {
    leftSamples.fill(0);
    rightSamples.fill(0);
}

}  // namespace Audio
