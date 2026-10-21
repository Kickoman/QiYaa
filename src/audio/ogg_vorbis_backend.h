#pragma once

#include <miniaudio.h>

#include <cstddef>

namespace Audio {

bool IsOgg(const char* head, size_t size);

ma_decoding_backend_vtable* OggVorbisBackend();

}  // namespace Audio
