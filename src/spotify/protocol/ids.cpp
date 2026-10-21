#include "spotify/protocol/ids.h"

#include <algorithm>

namespace SpotifyProtocol {

namespace {

constexpr std::string_view kDigits = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr std::size_t kBase62Size = 22;

}  // namespace

std::optional<TGid> GidFromBase62(std::string_view text) {
    if (text.size() != kBase62Size) {
        return std::nullopt;
    }
    TGid gid{};
    for (const char c : text) {
        const std::size_t digit = kDigits.find(c);
        if (digit == std::string_view::npos) {
            return std::nullopt;
        }
        std::uint32_t carry = static_cast<std::uint32_t>(digit);
        for (std::size_t i = gid.size(); i > 0; --i) {
            carry += static_cast<std::uint32_t>(gid[i - 1]) * 62;
            gid[i - 1] = static_cast<std::uint8_t>(carry);
            carry >>= 8;
        }
        if (carry != 0) {
            return std::nullopt;
        }
    }
    return gid;
}

std::string Base62FromGid(const TGid& gid) {
    TGid value = gid;
    std::string text;
    for (std::size_t n = 0; n < kBase62Size; ++n) {
        std::uint32_t remainder = 0;
        for (std::uint8_t& byte : value) {
            const std::uint32_t current = remainder << 8 | byte;
            byte = static_cast<std::uint8_t>(current / 62);
            remainder = current % 62;
        }
        text.push_back(kDigits[remainder]);
    }
    std::reverse(text.begin(), text.end());
    return text;
}

}  // namespace SpotifyProtocol
