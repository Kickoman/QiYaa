#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Spotify {

using TGid = std::array<std::uint8_t, 16>;
using TFileId = std::array<std::uint8_t, 20>;

std::optional<TGid> GidFromBase62(std::string_view text);
std::string Base62FromGid(const TGid& gid);

}  // namespace Spotify
