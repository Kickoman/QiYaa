#include "spotify/protocol/shannon.h"

namespace SpotifyProtocol {

namespace {

constexpr std::uint32_t kInitKonst = 0x6996c53a;
constexpr std::size_t kKeyWord = 13;

std::uint32_t Rotl(std::uint32_t w, int x) {
    return w << x | w >> (32 - x);
}

std::uint32_t Sbox1(std::uint32_t w) {
    w ^= Rotl(w, 5) | Rotl(w, 7);
    w ^= Rotl(w, 19) | Rotl(w, 22);
    return w;
}

std::uint32_t Sbox2(std::uint32_t w) {
    w ^= Rotl(w, 7) | Rotl(w, 22);
    w ^= Rotl(w, 5) | Rotl(w, 19);
    return w;
}

std::uint32_t ReadLe(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) | static_cast<std::uint32_t>(data[1]) << 8
        | static_cast<std::uint32_t>(data[2]) << 16 | static_cast<std::uint32_t>(data[3]) << 24;
}

void WriteLe(std::uint8_t* data, std::uint32_t value) {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8);
    data[2] = static_cast<std::uint8_t>(value >> 16);
    data[3] = static_cast<std::uint8_t>(value >> 24);
}

}  // namespace

Shannon::Shannon(ByteView key) {
    r[0] = 1;
    r[1] = 1;
    for (std::size_t i = 2; i < kWords; ++i) {
        r[i] = r[i - 1] + r[i - 2];
    }
    konst = kInitKonst;
    loadKey(key);
    konst = r[0];
    initialR = r;
}

void Shannon::nonce(std::uint32_t value) {
    const std::uint8_t bytes[4] = {
        static_cast<std::uint8_t>(value >> 24),
        static_cast<std::uint8_t>(value >> 16),
        static_cast<std::uint8_t>(value >> 8),
        static_cast<std::uint8_t>(value),
    };
    r = initialR;
    konst = kInitKonst;
    loadKey({bytes, sizeof bytes});
    konst = r[0];
    pendingBits = 0;
}

void Shannon::cycle() {
    std::uint32_t t = r[12] ^ r[13] ^ konst;
    t = Sbox1(t) ^ Rotl(r[0], 1);
    for (std::size_t i = 1; i < kWords; ++i) {
        r[i - 1] = r[i];
    }
    r[kWords - 1] = t;
    t = Sbox2(r[2] ^ r[15]);
    r[0] ^= t;
    streamWord = t ^ r[8] ^ r[12];
}

void Shannon::diffuse() {
    for (std::size_t i = 0; i < kWords; ++i) {
        cycle();
    }
}

void Shannon::loadKey(ByteView key) {
    for (std::size_t offset = 0; offset < key.size; offset += 4) {
        std::uint8_t word[4] = {};
        for (std::size_t i = 0; i < 4 && offset + i < key.size; ++i) {
            word[i] = key.data[offset + i];
        }
        r[kKeyWord] ^= ReadLe(word);
        cycle();
    }
    r[kKeyWord] ^= static_cast<std::uint32_t>(key.size);
    cycle();
    crcRegister = r;
    diffuse();
    for (std::size_t i = 0; i < kWords; ++i) {
        r[i] ^= crcRegister[i];
    }
}

void Shannon::crc(std::uint32_t word) {
    const std::uint32_t t = crcRegister[0] ^ crcRegister[2] ^ crcRegister[15] ^ word;
    for (std::size_t i = 1; i < kWords; ++i) {
        crcRegister[i - 1] = crcRegister[i];
    }
    crcRegister[kWords - 1] = t;
}

void Shannon::mac(std::uint32_t word) {
    crc(word);
    r[kKeyWord] ^= word;
}

template <bool Encrypt>
void Shannon::process(std::uint8_t* data, std::size_t size) {
    if (pendingBits != 0) {
        while (pendingBits != 0 && size != 0) {
            if (Encrypt) {
                macWord ^= static_cast<std::uint32_t>(*data) << (32 - pendingBits);
                *data ^= static_cast<std::uint8_t>(streamWord >> (32 - pendingBits));
            } else {
                *data ^= static_cast<std::uint8_t>(streamWord >> (32 - pendingBits));
                macWord ^= static_cast<std::uint32_t>(*data) << (32 - pendingBits);
            }
            ++data;
            --size;
            pendingBits -= 8;
        }
        if (pendingBits != 0) {
            return;
        }
        mac(macWord);
    }
    for (; size >= 4; data += 4, size -= 4) {
        cycle();
        std::uint32_t word = ReadLe(data);
        if (Encrypt) {
            mac(word);
            word ^= streamWord;
        } else {
            word ^= streamWord;
            mac(word);
        }
        WriteLe(data, word);
    }
    if (size != 0) {
        cycle();
        macWord = 0;
        pendingBits = 32;
        for (; size != 0; ++data, --size) {
            if (Encrypt) {
                macWord ^= static_cast<std::uint32_t>(*data) << (32 - pendingBits);
                *data ^= static_cast<std::uint8_t>(streamWord >> (32 - pendingBits));
            } else {
                *data ^= static_cast<std::uint8_t>(streamWord >> (32 - pendingBits));
                macWord ^= static_cast<std::uint32_t>(*data) << (32 - pendingBits);
            }
            pendingBits -= 8;
        }
    }
}

void Shannon::encrypt(std::uint8_t* data, std::size_t size) {
    process<true>(data, size);
}

void Shannon::decrypt(std::uint8_t* data, std::size_t size) {
    process<false>(data, size);
}

void Shannon::finish(std::uint8_t* output, std::size_t size) {
    if (pendingBits != 0) {
        mac(macWord);
    }
    cycle();
    r[kKeyWord] ^= kInitKonst ^ static_cast<std::uint32_t>(pendingBits << 3);
    pendingBits = 0;
    for (std::size_t i = 0; i < kWords; ++i) {
        r[i] ^= crcRegister[i];
    }
    diffuse();
    for (std::size_t offset = 0; offset < size; offset += 4) {
        cycle();
        for (std::size_t i = 0; i < 4 && offset + i < size; ++i) {
            output[offset + i] = static_cast<std::uint8_t>(streamWord >> (8 * i));
        }
    }
}

}  // namespace SpotifyProtocol
