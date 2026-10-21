#pragma once

#include "spotify/bytes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>

namespace Spotify {

using TSha1 = std::array<std::uint8_t, 20>;
using TAesKey = std::array<std::uint8_t, 16>;

TSha1 Sha1(std::initializer_list<ByteView> parts);
TSha1 HmacSha1(ByteView key, std::initializer_list<ByteView> parts);

bool VerifyRsaSha1(ByteView modulus, std::uint32_t exponent, const TSha1& digest, ByteView signature);

std::optional<TBytes> ModPow(ByteView base, ByteView exponent, ByteView modulus);

class AesCtr {
public:
    AesCtr(const TAesKey& key, const std::array<std::uint8_t, 16>& iv);
    ~AesCtr();
    AesCtr(const AesCtr&) = delete;
    AesCtr& operator=(const AesCtr&) = delete;

    void apply(std::uint8_t* data, std::size_t size, std::uint64_t offset) const;

private:
    struct Keys;

    void run(std::uint64_t block, std::uint8_t* data, std::size_t size) const;

    std::unique_ptr<Keys> keys;
    std::array<std::uint8_t, 16> iv;
};

}  // namespace Spotify
