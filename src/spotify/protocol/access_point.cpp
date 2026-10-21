#include "spotify/protocol/access_point.h"

#include "spotify/protocol/protobuf.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace SpotifyProtocol {

namespace {

constexpr std::uint8_t kDhPrime[] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc9, 0x0f, 0xda, 0xa2, 0x21, 0x68, 0xc2, 0x34,
    0xc4, 0xc6, 0x62, 0x8b, 0x80, 0xdc, 0x1c, 0xd1, 0x29, 0x02, 0x4e, 0x08, 0x8a, 0x67, 0xcc, 0x74,
    0x02, 0x0b, 0xbe, 0xa6, 0x3b, 0x13, 0x9b, 0x22, 0x51, 0x4a, 0x08, 0x79, 0x8e, 0x34, 0x04, 0xdd,
    0xef, 0x95, 0x19, 0xb3, 0xcd, 0x3a, 0x43, 0x1b, 0x30, 0x2b, 0x0a, 0x6d, 0xf2, 0x5f, 0x14, 0x37,
    0x4f, 0xe1, 0x35, 0x6d, 0x6d, 0x51, 0xc2, 0x45, 0xe4, 0x85, 0xb5, 0x76, 0x62, 0x5e, 0x7e, 0xc6,
    0xf4, 0x4c, 0x42, 0xe9, 0xa6, 0x3a, 0x36, 0x20, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

constexpr std::uint8_t kServerKey[] = {
    0xac, 0xe0, 0x46, 0x0b, 0xff, 0xc2, 0x30, 0xaf, 0xf4, 0x6b, 0xfe, 0xc3, 0xbf, 0xbf, 0x86, 0x3d, 0xa1, 0x91, 0xc6,
    0xcc, 0x33, 0x6c, 0x93, 0xa1, 0x4f, 0xb3, 0xb0, 0x16, 0x12, 0xac, 0xac, 0x6a, 0xf1, 0x80, 0xe7, 0xf6, 0x14, 0xd9,
    0x42, 0x9d, 0xbe, 0x2e, 0x34, 0x66, 0x43, 0xe3, 0x62, 0xd2, 0x32, 0x7a, 0x1a, 0x0d, 0x92, 0x3b, 0xae, 0xdd, 0x14,
    0x02, 0xb1, 0x81, 0x55, 0x05, 0x61, 0x04, 0xd5, 0x2c, 0x96, 0xa4, 0x4c, 0x1e, 0xcc, 0x02, 0x4a, 0xd4, 0xb2, 0x0c,
    0x00, 0x1f, 0x17, 0xed, 0xc2, 0x2f, 0xc4, 0x35, 0x21, 0xc8, 0xf0, 0xcb, 0xae, 0xd2, 0xad, 0xd7, 0x2b, 0x0f, 0x9d,
    0xb3, 0xc5, 0x32, 0x1a, 0x2a, 0xfe, 0x59, 0xf3, 0x5a, 0x0d, 0xac, 0x68, 0xf1, 0xfa, 0x62, 0x1e, 0xfb, 0x2c, 0x8d,
    0x0c, 0xb7, 0x39, 0x2d, 0x92, 0x47, 0xe3, 0xd7, 0x35, 0x1a, 0x6d, 0xbd, 0x24, 0xc2, 0xae, 0x25, 0x5b, 0x88, 0xff,
    0xab, 0x73, 0x29, 0x8a, 0x0b, 0xcc, 0xcd, 0x0c, 0x58, 0x67, 0x31, 0x89, 0xe8, 0xbd, 0x34, 0x80, 0x78, 0x4a, 0x5f,
    0xc9, 0x6b, 0x89, 0x9d, 0x95, 0x6b, 0xfc, 0x86, 0xd7, 0x4f, 0x33, 0xa6, 0x78, 0x17, 0x96, 0xc9, 0xc3, 0x2d, 0x0d,
    0x32, 0xa5, 0xab, 0xcd, 0x05, 0x27, 0xe2, 0xf7, 0x10, 0xa3, 0x96, 0x13, 0xc4, 0x2f, 0x99, 0xc0, 0x27, 0xbf, 0xed,
    0x04, 0x9c, 0x3c, 0x27, 0x58, 0x04, 0xb6, 0xb2, 0x19, 0xf9, 0xc1, 0x2f, 0x02, 0xe9, 0x48, 0x63, 0xec, 0xa1, 0xb6,
    0x42, 0xa0, 0x9d, 0x48, 0x25, 0xf8, 0xb3, 0x9d, 0xd0, 0xe8, 0x6a, 0xf9, 0x48, 0x4d, 0xa1, 0xc2, 0xba, 0x86, 0x30,
    0x42, 0xea, 0x9d, 0xb3, 0x08, 0x6c, 0x19, 0x0e, 0x48, 0xb3, 0x9d, 0x66, 0xeb, 0x00, 0x06, 0xa2, 0x5a, 0xee, 0xa1,
    0x1b, 0x13, 0x87, 0x3c, 0xd7, 0x19, 0xe6, 0x55, 0xbd,
};

constexpr std::size_t kDhPrivateSize = 95;
constexpr std::size_t kNonceSize = 16;
constexpr std::size_t kMacSize = 4;
constexpr std::size_t kMaxHandshakePacket = 64 * 1024;

enum Command : std::uint8_t {
    kPing = 0x04,
    kRequestKey = 0x0c,
    kAesKey = 0x0d,
    kAesKeyError = 0x0e,
    kCountryCode = 0x1b,
    kPong = 0x49,
    kPongAck = 0x4a,
    kProductInfo = 0x50,
    kLogin = 0xab,
    kApWelcome = 0xac,
    kAuthFailure = 0xad,
};

TBytes StripLeadingZeros(TBytes bytes) {
    const auto first = std::find_if(bytes.begin(), bytes.end(), [](std::uint8_t byte) { return byte != 0; });
    bytes.erase(bytes.begin(), first);
    return bytes;
}

struct DhChallenge {
    TBytes gs;
    TBytes signature;
    std::optional<ApLoginFailed> failed;
};

std::optional<ApLoginFailed> ParseLoginFailed(ByteView data) {
    ApLoginFailed failed;
    ProtoReader reader(data);
    while (reader.next()) {
        if (reader.field() == 10) {
            failed.code = static_cast<LoginError>(reader.varint());
        } else if (reader.field() == 40) {
            failed.description = reader.string();
        }
    }
    if (reader.failed()) {
        return std::nullopt;
    }
    return failed;
}

std::optional<DhChallenge> ParseApResponse(ByteView data) {
    DhChallenge challenge;
    ProtoReader response(data);
    while (response.next()) {
        if (response.field() == 30) {
            challenge.failed = ParseLoginFailed(response.bytes());
            continue;
        }
        if (response.field() != 10) {
            continue;
        }
        ProtoReader apChallenge(response.bytes());
        while (apChallenge.next()) {
            if (apChallenge.field() != 10) {
                continue;
            }
            ProtoReader cryptoUnion(apChallenge.bytes());
            while (cryptoUnion.next()) {
                if (cryptoUnion.field() != 10) {
                    continue;
                }
                ProtoReader dh(cryptoUnion.bytes());
                while (dh.next()) {
                    if (dh.field() == 10) {
                        challenge.gs = dh.bytes().copy();
                    } else if (dh.field() == 30) {
                        challenge.signature = dh.bytes().copy();
                    }
                }
            }
        }
    }
    if (response.failed()) {
        return std::nullopt;
    }
    return challenge;
}

std::optional<ApLoggedIn> ParseWelcome(ByteView data) {
    ApLoggedIn welcome;
    ProtoReader reader(data);
    while (reader.next()) {
        switch (reader.field()) {
            case 10: welcome.reusable.username = reader.string(); break;
            case 30: welcome.reusable.type = static_cast<AuthenticationType>(reader.varint()); break;
            case 40: welcome.reusable.data = reader.bytes().copy(); break;
            default: break;
        }
    }
    if (reader.failed()) {
        return std::nullopt;
    }
    return welcome;
}

std::string ProductType(ByteView info) {
    const std::string_view xml(reinterpret_cast<const char*>(info.data), info.size);
    constexpr std::string_view kOpen = "<type>";
    const std::size_t start = xml.find(kOpen);
    if (start == std::string_view::npos) {
        return {};
    }
    const std::size_t end = xml.find("</type>", start + kOpen.size());
    if (end == std::string_view::npos) {
        return {};
    }
    return std::string(xml.substr(start + kOpen.size(), end - start - kOpen.size()));
}

}  // namespace

Credentials Credentials::FromAccessToken(std::string_view token) {
    return Credentials{.username = {}, .type = AuthenticationType::SpotifyToken, .data = ByteView::Of(token).copy()};
}

ServerKey SpotifyServerKey() {
    return ServerKey{.modulus = TBytes(std::begin(kServerKey), std::end(kServerKey)), .exponent = 65537};
}

AccessPoint::AccessPoint(
    ClientIdentity clientIdentity,
    Credentials loginCredentials,
    TRandom randomBytes,
    ServerKey accessPointKey
)
    : identity(std::move(clientIdentity))
    , credentials(std::move(loginCredentials))
    , random(std::move(randomBytes))
    , serverKey(std::move(accessPointKey)) { }

void AccessPoint::start() {
    dhPrivate.assign(kDhPrivateSize, 0);
    random(dhPrivate.data(), dhPrivate.size());
    const std::uint8_t generator[] = {2};
    const std::optional<TBytes> gc = ModPow({generator, 1}, dhPrivate, {kDhPrime, sizeof kDhPrime});
    if (!gc) {
        fail("could not compute the key exchange");
        return;
    }
    std::uint8_t nonce[kNonceSize];
    random(nonce, sizeof nonce);
    const std::uint8_t padding[] = {0x1e};

    ProtoWriter buildInfo;
    buildInfo.varint(10, 0).varint(20, 0).varint(30, static_cast<std::uint32_t>(identity.platform));
    buildInfo.varint(40, identity.protocolVersion);
    ProtoWriter dhHello;
    dhHello.bytes(10, StripLeadingZeros(*gc)).varint(20, 1);
    ProtoWriter cryptoHello;
    cryptoHello.message(10, dhHello);
    ProtoWriter hello;
    hello.message(10, buildInfo).varint(30, 0).message(50, cryptoHello);
    hello.bytes(60, {nonce, sizeof nonce}).bytes(70, {padding, sizeof padding});

    TBytes packet = {0x00, 0x04};
    AppendU32(packet, static_cast<std::uint32_t>(2 + 4 + hello.data().size()));
    packet.insert(packet.end(), hello.data().begin(), hello.data().end());
    transcript = packet;
    outgoing.insert(outgoing.end(), packet.begin(), packet.end());
    stage = Stage::Handshake;
}

void AccessPoint::receive(ByteView bytes, std::int64_t nowMs) {
    if (stage == Stage::Failed || stage == Stage::Idle) {
        return;
    }
    incoming.insert(incoming.end(), bytes.data, bytes.data + bytes.size);
    if (stage == Stage::Handshake) {
        readHandshake();
    }
    if (stage == Stage::Login || stage == Stage::Ready) {
        readPackets(nowMs);
    }
}

void AccessPoint::advance(std::int64_t nowMs) {
    if (stage != Stage::Ready || !keepAliveDeadline || nowMs < *keepAliveDeadline) {
        return;
    }
    if (keepAlive != KeepAlive::PendingPong) {
        fail("the access point stopped answering");
        return;
    }
    const std::uint8_t zeros[4] = {};
    sendPacket(kPong, {zeros, sizeof zeros});
    keepAlive = KeepAlive::ExpectingPongAck;
    keepAliveDeadline = nowMs + kPongAckMs;
}

std::optional<std::int64_t> AccessPoint::deadline() const {
    if (stage != Stage::Ready) {
        return std::nullopt;
    }
    return keepAliveDeadline;
}

TBytes AccessPoint::takeOutgoing() {
    return std::exchange(outgoing, {});
}

std::vector<TApEvent> AccessPoint::takeEvents() {
    return std::exchange(events, {});
}

void AccessPoint::readHandshake() {
    if (incoming.size() < 4) {
        return;
    }
    const std::uint32_t size = ReadU32(incoming.data());
    if (size < 4 || size > kMaxHandshakePacket) {
        fail("bad handshake packet size");
        return;
    }
    if (incoming.size() < size) {
        return;
    }
    transcript.insert(transcript.end(), incoming.begin(), incoming.begin() + size);
    const TBytes reply(incoming.begin() + 4, incoming.begin() + size);
    incoming.erase(incoming.begin(), incoming.begin() + size);
    answerChallenge(reply);
}

void AccessPoint::answerChallenge(ByteView reply) {
    const std::optional<DhChallenge> challenge = ParseApResponse(reply);
    if (!challenge) {
        fail("malformed handshake reply");
        return;
    }
    if (challenge->failed) {
        events.push_back(*challenge->failed);
        stage = Stage::Failed;
        return;
    }
    if (challenge->gs.empty() || challenge->signature.empty()) {
        fail("handshake reply has no key");
        return;
    }
    if (!VerifyRsaSha1(serverKey.modulus, serverKey.exponent, Sha1({challenge->gs}), challenge->signature)) {
        fail("the access point's key is not signed by Spotify");
        return;
    }
    const std::optional<TBytes> shared = ModPow(challenge->gs, dhPrivate, {kDhPrime, sizeof kDhPrime});
    if (!shared) {
        fail("bad key from the access point");
        return;
    }
    const TBytes secret = StripLeadingZeros(*shared);
    TBytes material;
    for (std::uint8_t i = 1; i < 6; ++i) {
        const TSha1 part = HmacSha1(secret, {transcript, {&i, 1}});
        material.insert(material.end(), part.begin(), part.end());
    }
    const TSha1 answer = HmacSha1({material.data(), 20}, {transcript});
    sendCipher.emplace(ByteView{material.data() + 20, 32});
    receiveCipher.emplace(ByteView{material.data() + 52, 32});
    dhPrivate.clear();
    transcript.clear();

    ProtoWriter dhResponse;
    dhResponse.bytes(10, answer);
    ProtoWriter loginResponse;
    loginResponse.message(10, dhResponse);
    ProtoWriter plaintext;
    plaintext.message(10, loginResponse).message(20, ProtoWriter()).message(30, ProtoWriter());
    AppendU32(outgoing, static_cast<std::uint32_t>(4 + plaintext.data().size()));
    outgoing.insert(outgoing.end(), plaintext.data().begin(), plaintext.data().end());

    stage = Stage::Login;
    sendLogin();
}

void AccessPoint::sendLogin() {
    ProtoWriter loginCredentials;
    if (!credentials.username.empty()) {
        loginCredentials.string(10, credentials.username);
    }
    loginCredentials.varint(20, static_cast<std::uint32_t>(credentials.type)).bytes(30, credentials.data);
    ProtoWriter systemInfo;
    systemInfo.varint(10, static_cast<std::uint32_t>(identity.cpuFamily));
    systemInfo.varint(60, static_cast<std::uint32_t>(identity.os));
    systemInfo.string(90, identity.systemInformation).string(100, identity.deviceId);
    ProtoWriter login;
    login.message(10, loginCredentials).message(50, systemInfo).string(70, identity.versionString);
    sendPacket(kLogin, login.data());
}

void AccessPoint::sendPacket(std::uint8_t command, ByteView payload) {
    TBytes packet;
    packet.reserve(3 + payload.size + kMacSize);
    packet.push_back(command);
    AppendU16(packet, static_cast<std::uint16_t>(payload.size));
    packet.insert(packet.end(), payload.data, payload.data + payload.size);
    sendCipher->nonce(sendNonce++);
    sendCipher->encrypt(packet.data(), packet.size());
    std::uint8_t mac[kMacSize];
    sendCipher->finish(mac, sizeof mac);
    packet.insert(packet.end(), mac, mac + sizeof mac);
    outgoing.insert(outgoing.end(), packet.begin(), packet.end());
}

void AccessPoint::readPackets(std::int64_t nowMs) {
    std::size_t consumed = 0;
    while (stage != Stage::Failed) {
        const std::size_t available = incoming.size() - consumed;
        if (!pendingCommand) {
            if (available < 3) {
                break;
            }
            std::uint8_t header[3];
            std::copy_n(incoming.begin() + static_cast<std::ptrdiff_t>(consumed), 3, header);
            receiveCipher->nonce(receiveNonce++);
            receiveCipher->decrypt(header, sizeof header);
            pendingCommand = header[0];
            pendingSize = ReadU16(header + 1);
            consumed += 3;
            continue;
        }
        if (available < pendingSize + kMacSize) {
            break;
        }
        std::uint8_t* body = incoming.data() + consumed;
        receiveCipher->decrypt(body, pendingSize);
        std::uint8_t mac[kMacSize];
        receiveCipher->finish(mac, sizeof mac);
        if (!std::equal(mac, mac + kMacSize, body + pendingSize)) {
            fail("packet authentication failed");
            break;
        }
        const std::uint8_t command = *pendingCommand;
        const TBytes payload(body, body + pendingSize);
        consumed += pendingSize + kMacSize;
        pendingCommand.reset();
        handlePacket(command, payload, nowMs);
    }
    incoming.erase(incoming.begin(), incoming.begin() + static_cast<std::ptrdiff_t>(consumed));
}

void AccessPoint::handlePacket(std::uint8_t command, ByteView payload, std::int64_t nowMs) {
    switch (command) {
        case kApWelcome: {
            const std::optional<ApLoggedIn> welcome = ParseWelcome(payload);
            if (!welcome) {
                fail("malformed welcome");
                return;
            }
            stage = Stage::Ready;
            keepAlive = KeepAlive::ExpectingPing;
            keepAliveDeadline = nowMs + kFirstPingMs;
            events.push_back(*welcome);
            return;
        }
        case kAuthFailure: {
            std::optional<ApLoginFailed> failed = ParseLoginFailed(payload);
            events.push_back(failed.value_or(ApLoginFailed{}));
            stage = Stage::Failed;
            return;
        }
        case kPing:
            keepAlive = KeepAlive::PendingPong;
            keepAliveDeadline = nowMs + kPongDelayMs;
            return;
        case kPongAck:
            keepAlive = KeepAlive::ExpectingPing;
            keepAliveDeadline = nowMs + kNextPingMs;
            return;
        case kProductInfo: events.push_back(ApProduct{ProductType(payload)}); return;
        case kCountryCode:
            events.push_back(ApCountry{std::string(reinterpret_cast<const char*>(payload.data), payload.size)});
            return;
        case kAesKey:
            if (payload.size == 4 + 16) {
                ApAudioKey key;
                key.sequence = ReadU32(payload.data);
                std::copy_n(payload.data + 4, key.key.size(), key.key.begin());
                events.push_back(key);
            }
            return;
        case kAesKeyError:
            if (payload.size >= 6) {
                events.push_back(ApAudioKeyRefused{ReadU32(payload.data), ReadU16(payload.data + 4)});
            }
            return;
        default: return;
    }
}

std::optional<std::uint32_t> AccessPoint::requestAudioKey(const TGid& track, const TFileId& file) {
    if (stage != Stage::Ready) {
        return std::nullopt;
    }
    const std::uint32_t sequence = nextKeySequence++;
    TBytes payload(file.begin(), file.end());
    payload.insert(payload.end(), track.begin(), track.end());
    AppendU32(payload, sequence);
    AppendU16(payload, 0);
    sendPacket(kRequestKey, payload);
    return sequence;
}

void AccessPoint::fail(std::string reason) {
    stage = Stage::Failed;
    events.push_back(ApFailure{std::move(reason)});
}

}  // namespace SpotifyProtocol
