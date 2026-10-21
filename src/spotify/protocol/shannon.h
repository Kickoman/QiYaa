#pragma once

#include "spotify/protocol/bytes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SpotifyProtocol {

class Shannon {
public:
    explicit Shannon(ByteView key);

    void nonce(std::uint32_t value);
    void encrypt(std::uint8_t* data, std::size_t size);
    void decrypt(std::uint8_t* data, std::size_t size);
    void finish(std::uint8_t* mac, std::size_t size);

private:
    static constexpr std::size_t kWords = 16;
    using TRegister = std::array<std::uint32_t, kWords>;

    void cycle();
    void diffuse();
    void loadKey(ByteView key);
    void crc(std::uint32_t word);
    void mac(std::uint32_t word);
    template <bool Encrypt>
    void process(std::uint8_t* data, std::size_t size);

    TRegister r{};
    TRegister crcRegister{};
    TRegister initialR{};
    std::uint32_t konst = 0;
    std::uint32_t streamWord = 0;
    std::uint32_t macWord = 0;
    std::size_t pendingBits = 0;
};

}  // namespace SpotifyProtocol
