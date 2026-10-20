#include "audio/file_byte_source.h"

#include <ios>
#include <system_error>
#include <utility>

namespace Audio {

std::shared_ptr<FileByteSource> FileByteSource::Open(const std::filesystem::path& path) {
    std::error_code error;
    const uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        return nullptr;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return nullptr;
    }
    return std::shared_ptr<FileByteSource>(new FileByteSource(std::move(file), static_cast<uint64_t>(size)));
}

FileByteSource::FileByteSource(std::ifstream file, uint64_t size)
    : stream(std::move(file))
    , fileSize(size) { }

size_t FileByteSource::read(uint64_t offset, void* destination, size_t size) const {
    std::lock_guard lock(mutex);
    if (offset >= fileSize || size == 0) {
        return 0;
    }
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset));
    if (!stream) {
        return 0;
    }
    stream.read(static_cast<char*>(destination), static_cast<std::streamsize>(size));
    return static_cast<size_t>(stream.gcount());
}

}  // namespace Audio
