#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace Audio {

class ByteSource {
public:
    virtual ~ByteSource() = default;

    virtual size_t read(uint64_t offset, void* destination, size_t size) const = 0;
    virtual uint64_t available() const = 0;
    virtual bool complete() const = 0;
};

class StreamBuffer : public ByteSource {
public:
    explicit StreamBuffer(std::function<void()> onChange);

    void append(const char* data, size_t byteCount);
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
