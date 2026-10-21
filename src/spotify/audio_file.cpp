#include "spotify/audio_file.h"

#include <cstring>

namespace Spotify {

namespace {

constexpr std::array<std::uint8_t, 16> kAudioIv = {
    0x72,
    0xe0,
    0x67,
    0xfb,
    0xdd,
    0xcb,
    0xcf,
    0x77,
    0xeb,
    0xe8,
    0xbc,
    0x64,
    0x3f,
    0x63,
    0x0d,
    0x93,
};
constexpr std::size_t kNormalisationOffset = 144;

float ReadFloatLe(const std::uint8_t* data) {
    const std::uint32_t bits = static_cast<std::uint32_t>(data[0]) | static_cast<std::uint32_t>(data[1]) << 8
        | static_cast<std::uint32_t>(data[2]) << 16 | static_cast<std::uint32_t>(data[3]) << 24;
    float value = 0;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

}  // namespace

AudioDecryptor::AudioDecryptor(const TAesKey& key)
    : cipher(key, kAudioIv) { }

void AudioDecryptor::apply(std::uint8_t* data, std::size_t size, std::uint64_t offset) const {
    cipher.apply(data, size, offset);
}

std::optional<Normalisation> ParseNormalisation(ByteView decryptedHead) {
    if (decryptedHead.size < kNormalisationOffset + 16) {
        return std::nullopt;
    }
    const std::uint8_t* data = decryptedHead.data + kNormalisationOffset;
    return Normalisation{ReadFloatLe(data), ReadFloatLe(data + 4), ReadFloatLe(data + 8), ReadFloatLe(data + 12)};
}

}  // namespace Spotify
