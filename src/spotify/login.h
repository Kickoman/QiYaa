#pragma once

#include "spotify/access_point.h"
#include "spotify/bytes.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace Spotify {

struct LinuxSystem {
    std::string release;
    std::string version;
    std::string hardware;
};

struct ClientTokenParams {
    std::string clientVersion;
    std::string clientId;
    std::string deviceId;
    LinuxSystem linuxSystem;
};

struct ClientTokenGranted {
    std::string token;
    std::int64_t refreshAfterSeconds = 0;
};

struct HashcashChallenge {
    TBytes prefix;
    int length = 0;
};

struct ClientTokenChallenge {
    std::string state;
    std::optional<HashcashChallenge> hashcash;
};

struct WebFailure {
    std::string reason;
};

using TClientTokenReply = std::variant<ClientTokenGranted, ClientTokenChallenge, WebFailure>;

struct HashcashSolution {
    std::array<std::uint8_t, 16> suffix{};
    std::chrono::nanoseconds took{};
};

struct Login5Ok {
    std::string username;
    std::string accessToken;
    std::int64_t expiresInSeconds = 0;
};

struct Login5Challenge {
    TBytes context;
    HashcashChallenge hashcash;
};

struct Login5Error {
    std::uint32_t code = 0;
};

using TLogin5Reply = std::variant<Login5Ok, Login5Challenge, Login5Error, WebFailure>;

TBytes ClientTokenRequest(const ClientTokenParams& params);
TBytes ClientTokenAnswer(const std::string& state, const HashcashSolution& solution);
TClientTokenReply ParseClientToken(ByteView body);

std::optional<HashcashSolution> SolveHashcash(ByteView context, const HashcashChallenge& challenge);

TBytes Login5Request(
    const std::string& clientId,
    const std::string& deviceId,
    const StoredCredentials& credentials,
    ByteView context,
    const std::optional<HashcashSolution>& solution
);
TLogin5Reply ParseLogin5(ByteView body);

}  // namespace Spotify
