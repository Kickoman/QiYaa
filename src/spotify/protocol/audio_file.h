#pragma once

#include "spotify/protocol/bytes.h"
#include "spotify/protocol/crypto.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace SpotifyProtocol {

constexpr std::size_t kOggHeaderSize = 0xa7;

struct Normalisation {
    float trackGainDb = 0;
    float trackPeak = 1;
    float albumGainDb = 0;
    float albumPeak = 1;
};

class AudioDecryptor {
public:
    explicit AudioDecryptor(const TAesKey& key);

    void apply(std::uint8_t* data, std::size_t size, std::uint64_t offset) const;

private:
    AesCtr cipher;
};

std::optional<Normalisation> ParseNormalisation(ByteView decryptedHead);

}  // namespace SpotifyProtocol
