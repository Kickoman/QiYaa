#pragma once

#include "spotify/bytes.h"
#include "spotify/crypto.h"
#include "spotify/ids.h"
#include "spotify/shannon.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace Spotify {

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

struct StoredCredentials {
    std::string username;
    std::uint32_t authType = 1;
    TBytes data;
};

struct ApLoggedIn {
    StoredCredentials reusable;
};

struct ApLoginFailed {
    std::uint32_t code = 0;
    std::string description;
};

struct ApCountry {
    std::string code;
};

struct ApAudioKey {
    std::uint32_t sequence = 0;
    TAesKey key{};
};

struct ApAudioKeyRefused {
    std::uint32_t sequence = 0;
    std::uint16_t code = 0;
};

struct ApPing { };

struct ApFailure {
    std::string reason;
};

using TApEvent = std::variant<ApLoggedIn, ApLoginFailed, ApCountry, ApAudioKey, ApAudioKeyRefused, ApPing, ApFailure>;

class AccessPoint {
public:
    using TRandom = std::function<void(std::uint8_t* data, std::size_t size)>;

    AccessPoint(ClientIdentity identity, StoredCredentials credentials, TRandom random);

    void start();
    void receive(ByteView bytes);
    TBytes takeOutgoing();
    std::vector<TApEvent> takeEvents();

    bool loggedIn() const { return stage == Stage::Ready; }
    std::optional<std::uint32_t> requestAudioKey(const TGid& track, const TFileId& file);
    void pong();

private:
    enum class Stage { Idle, Handshake, Login, Ready, Failed };

    void readHandshake();
    void readPackets();
    void answerChallenge(ByteView reply);
    void sendLogin();
    void sendPacket(std::uint8_t command, ByteView payload);
    void handlePacket(std::uint8_t command, ByteView payload);
    void fail(std::string reason);

    ClientIdentity identity;
    StoredCredentials credentials;
    TRandom random;
    Stage stage = Stage::Idle;
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

}  // namespace Spotify
