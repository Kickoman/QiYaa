#pragma once

#include <cstddef>
#include <cstdint>

namespace Audio {

class ByteSource {
public:
    virtual ~ByteSource() = default;

    virtual size_t read(uint64_t offset, void* destination, size_t size) const = 0;
    virtual uint64_t available() const = 0;
    virtual bool complete() const = 0;
};

}  // namespace Audio
