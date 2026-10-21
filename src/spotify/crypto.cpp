#include "spotify/crypto.h"

#include "bearssl.h"
#include "spotify/modpow.h"

#include <algorithm>
#include <cstring>

namespace Spotify {

struct AesCtr::Keys {
    br_aes_ct_ctr_keys aes;
};

TSha1 Sha1(std::initializer_list<ByteView> parts) {
    br_sha1_context context;
    br_sha1_init(&context);
    for (const ByteView& part : parts) {
        br_sha1_update(&context, part.data, part.size);
    }
    TSha1 digest{};
    br_sha1_out(&context, digest.data());
    return digest;
}

TSha1 HmacSha1(ByteView key, std::initializer_list<ByteView> parts) {
    br_hmac_key_context keyContext;
    br_hmac_key_init(&keyContext, &br_sha1_vtable, key.data, key.size);
    br_hmac_context context;
    br_hmac_init(&context, &keyContext, 0);
    for (const ByteView& part : parts) {
        br_hmac_update(&context, part.data, part.size);
    }
    TSha1 digest{};
    br_hmac_out(&context, digest.data());
    return digest;
}

bool VerifyRsaSha1(ByteView modulus, std::uint32_t exponent, const TSha1& digest, ByteView signature) {
    if (signature.size > modulus.size) {
        return false;
    }
    TBytes n = modulus.copy();
    TBytes e;
    AppendU32(e, exponent);
    e.erase(e.begin(), std::find_if(e.begin(), e.end(), [](std::uint8_t byte) { return byte != 0; }));
    TBytes padded(modulus.size - signature.size, 0);
    padded.insert(padded.end(), signature.data, signature.data + signature.size);
    br_rsa_public_key key{n.data(), n.size(), e.data(), e.size()};
    std::uint8_t recovered[20];
    if (br_rsa_i31_pkcs1_vrfy(padded.data(), padded.size(), BR_HASH_OID_SHA1, sizeof recovered, &key, recovered) != 1) {
        return false;
    }
    return std::memcmp(recovered, digest.data(), sizeof recovered) == 0;
}

std::optional<TBytes> ModPow(ByteView base, ByteView exponent, ByteView modulus) {
    TBytes result(modulus.size);
    const int ok =
        SpotifyModPow(result.data(), base.data, base.size, exponent.data, exponent.size, modulus.data, modulus.size);
    if (ok != 1) {
        return std::nullopt;
    }
    return result;
}

AesCtr::AesCtr(const TAesKey& key, const std::array<std::uint8_t, 16>& initialCounter)
    : keys(std::make_unique<Keys>())
    , iv(initialCounter) {
    br_aes_ct_ctr_init(&keys->aes, key.data(), key.size());
}

AesCtr::~AesCtr() = default;

void AesCtr::apply(std::uint8_t* data, std::size_t size, std::uint64_t offset) const {
    const std::size_t within = static_cast<std::size_t>(offset % 16);
    if (within != 0 && size > 0) {
        const std::size_t head = std::min<std::size_t>(16 - within, size);
        std::uint8_t block[16] = {};
        std::memcpy(block + within, data, head);
        run(offset / 16, block, sizeof block);
        std::memcpy(data, block + within, head);
        data += head;
        size -= head;
        offset += head;
    }
    run(offset / 16, data, size);
}

void AesCtr::run(std::uint64_t block, std::uint8_t* data, std::size_t size) const {
    while (size > 0) {
        std::array<std::uint8_t, 16> counter = iv;
        std::uint64_t carry = block;
        for (int i = 15; i >= 0 && carry != 0; --i) {
            carry += counter[static_cast<std::size_t>(i)];
            counter[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(carry);
            carry >>= 8;
        }
        const std::uint32_t low = ReadU32(counter.data() + 12);
        const std::uint64_t blocksBeforeWrap = (std::uint64_t{1} << 32) - low;
        const std::uint64_t chunk = std::min<std::uint64_t>(size, blocksBeforeWrap * 16);
        br_aes_ct_ctr_run(&keys->aes, counter.data(), low, data, static_cast<std::size_t>(chunk));
        data += chunk;
        size -= static_cast<std::size_t>(chunk);
        block += (chunk + 15) / 16;
    }
}

}  // namespace Spotify
