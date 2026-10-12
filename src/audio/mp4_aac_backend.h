#pragma once

#include <miniaudio.h>

#include <cstddef>

namespace Audio {

// Whether the first bytes of a stream are an MP4 file: "ftyp" at offset 4.
bool IsMp4(const char* head, size_t size);

// A miniaudio decoding backend for AAC (LC, HE-AAC, HE-AAC v2) in an MP4 file.
ma_decoding_backend_vtable* Mp4AacBackend();

}  // namespace Audio
