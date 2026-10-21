#include "spotify/protocol/bytes.h"

namespace SpotifyProtocol {

namespace {

int HexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

int Base64Digit(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+' || c == '-') {
        return 62;
    }
    if (c == '/' || c == '_') {
        return 63;
    }
    return -1;
}

}  // namespace

ByteView ByteView::Of(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

ByteView ByteView::sub(std::size_t offset, std::size_t count) const {
    if (offset > size) {
        return {};
    }
    const std::size_t available = size - offset;
    return {data + offset, count < available ? count : available};
}

TBytes ByteView::copy() const {
    return TBytes(data, data + size);
}

std::string ToHex(ByteView bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(bytes.size * 2);
    for (std::size_t i = 0; i < bytes.size; ++i) {
        hex.push_back(kDigits[bytes.data[i] >> 4]);
        hex.push_back(kDigits[bytes.data[i] & 0x0f]);
    }
    return hex;
}

std::optional<TBytes> FromHex(std::string_view hex) {
    if (hex.size() % 2 != 0) {
        return std::nullopt;
    }
    TBytes bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int high = HexDigit(hex[i]);
        const int low = HexDigit(hex[i + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        bytes.push_back(static_cast<std::uint8_t>(high << 4 | low));
    }
    return bytes;
}

std::optional<TBytes> FromBase64(std::string_view text) {
    TBytes bytes;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char c : text) {
        if (c == '=' || c == '\n' || c == '\r') {
            continue;
        }
        const int digit = Base64Digit(c);
        if (digit < 0) {
            return std::nullopt;
        }
        buffer = buffer << 6 | static_cast<std::uint32_t>(digit);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes.push_back(static_cast<std::uint8_t>(buffer >> bits));
        }
    }
    return bytes;
}

void AppendU16(TBytes& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void AppendU32(TBytes& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

std::uint16_t ReadU16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0] << 8 | data[1]);
}

std::uint32_t ReadU32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) << 24 | static_cast<std::uint32_t>(data[1]) << 16
        | static_cast<std::uint32_t>(data[2]) << 8 | data[3];
}

}  // namespace SpotifyProtocol
