#include "spotify/protocol/modpow.h"

#include "inner.h"

enum { kMaxModulusBytes = 512, kWords = kMaxModulusBytes * 8 / 31 + 3 };

int SpotifyModPow(
    unsigned char* result,
    const unsigned char* base,
    size_t baseSize,
    const unsigned char* exponent,
    size_t exponentSize,
    const unsigned char* modulus,
    size_t modulusSize
) {
    uint32_t m[kWords];
    uint32_t x[kWords];
    uint32_t scratch[kWords * 8];
    uint32_t m0i;
    if (modulusSize == 0 || modulusSize > kMaxModulusBytes || (modulus[modulusSize - 1] & 1) == 0) {
        return 0;
    }
    br_i31_decode(m, modulus, modulusSize);
    m0i = br_i31_ninv31(m[1]);
    if (br_i31_decode_mod(x, base, baseSize, m) != 1) {
        return 0;
    }
    if (br_i31_modpow_opt(x, exponent, exponentSize, m, m0i, scratch, sizeof scratch / sizeof scratch[0]) != 1) {
        return 0;
    }
    br_i31_encode(result, modulusSize, x);
    return 1;
}
