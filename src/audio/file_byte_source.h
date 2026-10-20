#pragma once

#include "audio/byte_source.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>

namespace Audio {

class FileByteSource : public ByteSource {
public:
    static std::shared_ptr<FileByteSource> Open(const std::filesystem::path& path);

    size_t read(uint64_t offset, void* destination, size_t size) const override;
    uint64_t available() const override { return fileSize; }
    bool complete() const override { return true; }

private:
    FileByteSource(std::ifstream file, uint64_t size);

    mutable std::mutex mutex;
    mutable std::ifstream stream;
    const uint64_t fileSize;
};

}  // namespace Audio
