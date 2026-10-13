#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace Audio {

// The bytes of one track, for the decoder. A reader keeps its own offset; nothing here waits, so the
// decoder can wait for bytes on its own terms (and stop waiting when it gets a command).
class ByteSource {
public:
    virtual ~ByteSource() = default;

    // Copies the bytes from `offset` that are here now, at most `size`; returns how many.
    virtual size_t read(uint64_t offset, void* destination, size_t size) const = 0;
    // How many bytes from the start are here now.
    virtual uint64_t available() const = 0;
    // No more bytes will come: available() is the whole source (or all a failed download got).
    virtual bool complete() const = 0;
};

// A download as it arrives, held whole in memory. Written by the UI thread, read by the decoder.
class StreamBuffer : public ByteSource {
public:
    // `changed` runs after every append() and finish(), outside the buffer's lock.
    explicit StreamBuffer(std::function<void()> onChange);

    void append(const char* data, size_t byteCount);
    // The download is over, whole or cut short: what is here is all there will be.
    void finish();

    size_t read(uint64_t offset, void* destination, size_t size) const override;
    uint64_t available() const override;
    bool complete() const override;

private:
    std::function<void()> changed;
    mutable std::mutex mutex;
    std::vector<char> bytes;
    bool isComplete = false;
};

}  // namespace Audio
