#include "spotify/protocol/login.h"

#include "spotify/protocol/crypto.h"
#include "spotify/protocol/protobuf.h"

#include <cctype>

namespace SpotifyProtocol {

namespace {

constexpr auto kHashcashTimeout = std::chrono::seconds(5);

std::uint64_t ReadU64(const std::uint8_t* data) {
    return static_cast<std::uint64_t>(ReadU32(data)) << 32 | ReadU32(data + 4);
}

void AppendU64(TBytes& out, std::uint64_t value) {
    AppendU32(out, static_cast<std::uint32_t>(value >> 32));
    AppendU32(out, static_cast<std::uint32_t>(value));
}

int TrailingZeros(std::uint64_t value) {
    if (value == 0) {
        return 64;
    }
    int count = 0;
    while ((value & 1) == 0) {
        value >>= 1;
        ++count;
    }
    return count;
}

std::string UpperHex(ByteView bytes) {
    std::string hex = ToHex(bytes);
    for (char& c : hex) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return hex;
}

std::optional<HashcashChallenge> ParseClientTokenHashcash(ByteView data) {
    std::optional<HashcashChallenge> hashcash;
    ProtoReader challenge(data);
    while (challenge.next()) {
        if (challenge.field() != 4) {
            continue;
        }
        HashcashChallenge parsed;
        std::string prefix;
        ProtoReader parameters(challenge.bytes());
        while (parameters.next()) {
            if (parameters.field() == 1) {
                parsed.length = static_cast<int>(parameters.varint());
            } else if (parameters.field() == 2) {
                prefix = parameters.string();
            }
        }
        std::optional<TBytes> bytes = FromHex(prefix);
        if (bytes) {
            parsed.prefix = std::move(*bytes);
            hashcash = parsed;
        }
    }
    return hashcash;
}

}  // namespace

TBytes ClientTokenRequest(const ClientTokenParams& params) {
    ProtoWriter platform;
    if (const auto* linuxData = std::get_if<LinuxPlatform>(&params.platform)) {
        ProtoWriter data;
        data.string(1, "Linux")
            .string(2, linuxData->release)
            .string(3, linuxData->version)
            .string(4, linuxData->hardware);
        platform.message(5, data);
    } else if (const auto* windows = std::get_if<WindowsPlatform>(&params.platform)) {
        ProtoWriter data;
        data.varint(1, static_cast<std::uint64_t>(windows->osVersion))
            .varint(3, static_cast<std::uint64_t>(windows->osBuild))
            .varint(4, 2)
            .varint(6, 9)
            .varint(7, static_cast<std::uint64_t>(windows->imageFileMachine))
            .varint(8, static_cast<std::uint64_t>(windows->peMachine))
            .varint(10, 1);
        platform.message(4, data);
    } else if (const auto* mac = std::get_if<MacPlatform>(&params.platform)) {
        ProtoWriter data;
        data.string(1, mac->systemVersion).string(2, "iMac21,1").string(3, mac->cpuType);
        platform.message(3, data);
    }
    ProtoWriter connectivity;
    connectivity.message(1, platform).string(2, params.deviceId);
    ProtoWriter clientData;
    clientData.string(1, params.clientVersion).string(2, params.clientId).message(3, connectivity);
    ProtoWriter request;
    request.varint(1, 1).message(2, clientData);
    return request.data();
}

TBytes ClientTokenAnswer(const std::string& state, const HashcashSolution& solution) {
    ProtoWriter hashcash;
    hashcash.string(1, UpperHex(solution.suffix));
    ProtoWriter answer;
    answer.varint(1, 3).message(4, hashcash);
    ProtoWriter answers;
    answers.string(1, state).message(2, answer);
    ProtoWriter request;
    request.varint(1, 2).message(3, answers);
    return request.data();
}

TClientTokenReply ParseClientToken(ByteView body) {
    ProtoReader reply(body);
    while (reply.next()) {
        if (reply.field() == 2) {
            ClientTokenGranted granted;
            ProtoReader token(reply.bytes());
            while (token.next()) {
                if (token.field() == 1) {
                    granted.token = token.string();
                } else if (token.field() == 3) {
                    granted.refreshAfterSeconds = static_cast<std::int64_t>(token.varint());
                }
            }
            if (!token.failed() && !granted.token.empty()) {
                return granted;
            }
        } else if (reply.field() == 3) {
            ClientTokenChallenge challenge;
            ProtoReader challenges(reply.bytes());
            while (challenges.next()) {
                if (challenges.field() == 1) {
                    challenge.state = challenges.string();
                } else if (challenges.field() == 2 && !challenge.hashcash) {
                    challenge.hashcash = ParseClientTokenHashcash(challenges.bytes());
                }
            }
            if (!challenges.failed()) {
                return challenge;
            }
        }
    }
    return WebFailure{"unexpected client token reply"};
}

std::optional<HashcashSolution> SolveHashcash(ByteView context, const HashcashChallenge& challenge) {
    const auto started = std::chrono::steady_clock::now();
    const TSha1 contextDigest = Sha1({context});
    const std::uint64_t target = ReadU64(contextDigest.data() + 12);
    for (std::uint64_t counter = 0;; ++counter) {
        if ((counter & 0xffff) == 0 && std::chrono::steady_clock::now() - started > kHashcashTimeout) {
            return std::nullopt;
        }
        TBytes suffix;
        AppendU64(suffix, target + counter);
        AppendU64(suffix, counter);
        const TSha1 digest = Sha1({challenge.prefix, suffix});
        if (TrailingZeros(ReadU64(digest.data() + 12)) >= challenge.length) {
            HashcashSolution solution;
            std::copy(suffix.begin(), suffix.end(), solution.suffix.begin());
            solution.took = std::chrono::steady_clock::now() - started;
            return solution;
        }
    }
}

TBytes Login5Request(
    const std::string& clientId,
    const std::string& deviceId,
    const Credentials& credentials,
    ByteView context,
    const std::optional<HashcashSolution>& solution
) {
    ProtoWriter clientInfo;
    clientInfo.string(1, clientId).string(2, deviceId);
    ProtoWriter stored;
    stored.string(1, credentials.username).bytes(2, credentials.data);
    ProtoWriter request;
    request.message(1, clientInfo);
    if (context.size != 0) {
        request.bytes(2, context);
    }
    if (solution) {
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(solution->took);
        const auto nanos = solution->took - seconds;
        ProtoWriter duration;
        if (seconds.count() != 0) {
            duration.varint(1, static_cast<std::uint64_t>(seconds.count()));
        }
        if (nanos.count() != 0) {
            duration.varint(2, static_cast<std::uint64_t>(nanos.count()));
        }
        ProtoWriter hashcash;
        hashcash.bytes(1, solution->suffix).message(2, duration);
        ProtoWriter one;
        one.message(1, hashcash);
        ProtoWriter solutions;
        solutions.message(1, one);
        request.message(3, solutions);
    }
    request.message(100, stored);
    return request.data();
}

TLogin5Reply ParseLogin5(ByteView body) {
    std::optional<Login5Ok> ok;
    std::optional<Login5Error> error;
    std::optional<HashcashChallenge> hashcash;
    TBytes context;
    ProtoReader reply(body);
    while (reply.next()) {
        switch (reply.field()) {
            case 1: {
                Login5Ok parsed;
                ProtoReader fields(reply.bytes());
                while (fields.next()) {
                    if (fields.field() == 1) {
                        parsed.username = fields.string();
                    } else if (fields.field() == 2) {
                        parsed.accessToken = fields.string();
                    } else if (fields.field() == 4) {
                        parsed.expiresInSeconds = static_cast<std::int64_t>(fields.varint());
                    }
                }
                ok = parsed;
                break;
            }
            case 2: error = Login5Error{static_cast<std::uint32_t>(reply.varint())}; break;
            case 3: {
                ProtoReader challenges(reply.bytes());
                while (challenges.next()) {
                    ProtoReader challenge(challenges.bytes());
                    while (challenge.next()) {
                        if (challenge.field() != 1) {
                            continue;
                        }
                        HashcashChallenge parsed;
                        ProtoReader fields(challenge.bytes());
                        while (fields.next()) {
                            if (fields.field() == 1) {
                                parsed.prefix = fields.bytes().copy();
                            } else if (fields.field() == 2) {
                                parsed.length = static_cast<int>(fields.varint());
                            }
                        }
                        hashcash = parsed;
                    }
                }
                break;
            }
            case 5: context = reply.bytes().copy(); break;
            default: break;
        }
    }
    if (reply.failed()) {
        return WebFailure{"malformed login5 reply"};
    }
    if (ok && !ok->accessToken.empty()) {
        return *ok;
    }
    if (error) {
        return *error;
    }
    if (hashcash) {
        return Login5Challenge{context, *hashcash};
    }
    return WebFailure{"unexpected login5 reply"};
}

}  // namespace SpotifyProtocol
