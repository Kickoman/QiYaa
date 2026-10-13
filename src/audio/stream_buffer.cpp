#include "audio/stream_buffer.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace Audio {

StreamBuffer::StreamBuffer(std::function<void()> onChange)
    : changed(std::move(onChange)) { }

void StreamBuffer::append(const char* data, size_t byteCount) {
    {
        std::lock_guard lock(mutex);
        bytes.insert(bytes.end(), data, data + byteCount);
    }
    changed();
}

void StreamBuffer::finish() {
    {
        std::lock_guard lock(mutex);
        isComplete = true;
    }
    changed();
}

size_t StreamBuffer::read(uint64_t offset, void* destination, size_t size) const {
    std::lock_guard lock(mutex);
    if (offset >= bytes.size()) {
        return 0;
    }
    const size_t count = std::min(size, static_cast<size_t>(bytes.size() - offset));
    std::memcpy(destination, bytes.data() + offset, count);
    return count;
}

uint64_t StreamBuffer::available() const {
    std::lock_guard lock(mutex);
    return bytes.size();
}

bool StreamBuffer::complete() const {
    std::lock_guard lock(mutex);
    return isComplete;
}

}  // namespace Audio
