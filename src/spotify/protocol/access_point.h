#pragma once

#include "spotify/protocol/bytes.h"
#include "spotify/protocol/crypto.h"
#include "spotify/protocol/ids.h"
#include "spotify/protocol/shannon.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace SpotifyProtocol {

enum class ApPlatform : std::uint32_t {
    Win32X86 = 0x00,
    OsxX86 = 0x01,
    LinuxX86 = 0x02,
    LinuxX86_64 = 0x08,
    OsxX86_64 = 0x09,
    LinuxArm = 0x11,
    Win32X86_64 = 0x27,
};

enum class ApCpuFamily : std::uint32_t { Unknown = 0, X86 = 1, X86_64 = 2, Arm = 5 };

enum class ApOs : std::uint32_t { Unknown = 0, Windows = 1, Osx = 2, Linux = 5, Android = 7 };

struct ClientIdentity {
    ApPlatform platform = ApPlatform::LinuxX86_64;
    ApCpuFamily cpuFamily = ApCpuFamily::X86_64;
    ApOs os = ApOs::Linux;
    std::uint64_t protocolVersion = 124200290;
    std::string systemInformation;
    std::string versionString;
    std::string deviceId;
};

enum class AuthenticationType : std::uint32_t { StoredCredentials = 1, SpotifyToken = 3 };

struct Credentials {
    std::string username;
    AuthenticationType type = AuthenticationType::StoredCredentials;
    TBytes data;

    static Credentials FromAccessToken(std::string_view token);
};

struct ServerKey {
    TBytes modulus;
    std::uint32_t exponent = 65537;
};

ServerKey SpotifyServerKey();

enum class LoginError : std::uint32_t {
    ProtocolError = 0,
    TryAnotherAp = 2,
    BadConnectionId = 5,
    TravelRestriction = 9,
    PremiumAccountRequired = 11,
    BadCredentials = 12,
    CouldNotValidateCredentials = 13,
    AccountExists = 14,
    ExtraVerificationRequired = 15,
    InvalidAppKey = 16,
    ApplicationBanned = 17,
};

struct ApLoggedIn {
    Credentials reusable;
};

struct ApLoginFailed {
    LoginError code = LoginError::ProtocolError;
    std::string description;
};

struct ApCountry {
    std::string code;
};

struct ApProduct {
    std::string type;
};

struct ApAudioKey {
    std::uint32_t sequence = 0;
    TAesKey key{};
};

struct ApAudioKeyRefused {
    std::uint32_t sequence = 0;
    std::uint16_t code = 0;
};

struct ApFailure {
    std::string reason;
};

using TApEvent =
    std::variant<ApLoggedIn, ApLoginFailed, ApCountry, ApProduct, ApAudioKey, ApAudioKeyRefused, ApFailure>;

class AccessPoint {
public:
    using TRandom = std::function<void(std::uint8_t* data, std::size_t size)>;

    static constexpr std::int64_t kFirstPingMs = 20'000;
    static constexpr std::int64_t kPongDelayMs = 60'000;
    static constexpr std::int64_t kPongAckMs = 20'000;
    static constexpr std::int64_t kNextPingMs = 80'000;

    AccessPoint(
        ClientIdentity identity,
        Credentials credentials,
        TRandom random,
        ServerKey serverKey = SpotifyServerKey()
    );

    void start();
    void receive(ByteView bytes, std::int64_t nowMs);
    void advance(std::int64_t nowMs);
    std::optional<std::int64_t> deadline() const;
    TBytes takeOutgoing();
    std::vector<TApEvent> takeEvents();

    bool loggedIn() const { return stage == Stage::Ready; }
    bool failed() const { return stage == Stage::Failed; }
    std::optional<std::uint32_t> requestAudioKey(const TGid& track, const TFileId& file);

private:
    enum class Stage { Idle, Handshake, Login, Ready, Failed };
    enum class KeepAlive { ExpectingPing, PendingPong, ExpectingPongAck };

    void readHandshake();
    void readPackets(std::int64_t nowMs);
    void answerChallenge(ByteView reply);
    void sendLogin();
    void sendPacket(std::uint8_t command, ByteView payload);
    void handlePacket(std::uint8_t command, ByteView payload, std::int64_t nowMs);
    void fail(std::string reason);

    ClientIdentity identity;
    Credentials credentials;
    TRandom random;
    ServerKey serverKey;
    Stage stage = Stage::Idle;
    KeepAlive keepAlive = KeepAlive::ExpectingPing;
    std::optional<std::int64_t> keepAliveDeadline;
    TBytes outgoing;
    TBytes incoming;
    TBytes transcript;
    TBytes dhPrivate;
    std::vector<TApEvent> events;
    std::optional<Shannon> sendCipher;
    std::optional<Shannon> receiveCipher;
    std::uint32_t sendNonce = 0;
    std::uint32_t receiveNonce = 0;
    std::optional<std::uint8_t> pendingCommand;
    std::size_t pendingSize = 0;
    std::uint32_t nextKeySequence = 0;
};

}  // namespace SpotifyProtocol
