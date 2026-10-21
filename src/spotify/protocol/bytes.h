#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace SpotifyProtocol {

using TBytes = std::vector<std::uint8_t>;

struct ByteView {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;

    ByteView() = default;
    ByteView(const std::uint8_t* bytes, std::size_t count)
        : data(bytes)
        , size(count) { }
    ByteView(const TBytes& bytes)
        : data(bytes.data())
        , size(bytes.size()) { }
    template <std::size_t N>
    ByteView(const std::array<std::uint8_t, N>& bytes)
        : data(bytes.data())
        , size(N) { }

    static ByteView Of(std::string_view text);
    ByteView sub(std::size_t offset, std::size_t count) const;
    TBytes copy() const;
};

std::string ToHex(ByteView bytes);
std::optional<TBytes> FromHex(std::string_view hex);
std::optional<TBytes> FromBase64(std::string_view text);

void AppendU16(TBytes& out, std::uint16_t value);
void AppendU32(TBytes& out, std::uint32_t value);
std::uint16_t ReadU16(const std::uint8_t* data);
std::uint32_t ReadU32(const std::uint8_t* data);

}  // namespace SpotifyProtocol
