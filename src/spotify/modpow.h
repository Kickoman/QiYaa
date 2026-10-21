#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int SpotifyModPow(
    unsigned char* result,
    const unsigned char* base,
    size_t baseSize,
    const unsigned char* exponent,
    size_t exponentSize,
    const unsigned char* modulus,
    size_t modulusSize
);

#ifdef __cplusplus
}
#endif
