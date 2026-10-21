#include "spotify/protocol/access_point.h"
#include "spotify/protocol/bytes.h"
#include "spotify/protocol/crypto.h"
#include "spotify/protocol/protobuf.h"
#include "spotify/protocol/shannon.h"

#include <QObject>
#include <QTest>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using SpotifyProtocol::ByteView;
using SpotifyProtocol::ProtoReader;
using SpotifyProtocol::ProtoWriter;
using SpotifyProtocol::TBytes;

const std::string kDhPrime =
    "ffffffffffffffffc90fdaa22168c234c4c6628b80dc1cd129024e088a67cc74020bbea63b139b22514a08798e34"
    "04ddef9519b3cd3a431b302b0a6df25f14374fe1356d6d51c245e485b576625e7ec6f44c42e9a63a3620ffffffff"
    "ffffffff";
const std::string kServerPrivate =
    "030a11181f262d343b424950575e656c737a81888f969da4abb2b9c0c7ced5dce3eaf1f8ff060d141b222930373e"
    "454c535a61686f767d848b9299a0a7aeb5bcc3cad1d8dfe6edf4fb020910171e252c333a41484f565d646b727980"
    "878e95";
const std::string kServerPublic =
    "d15d5ac9ee0a866599a2aaaceeb7f1ec6caa1d2e0420c2fc3a8b2b81ed46632109be52227c4c0216866af715977c"
    "e95a139c8890c511e2eff06400dc9f946c23aac8287f1a3e847c804802449ac74d090498818f1cf318eb22315c73"
    "8de00134";
const std::string kTestKeyModulus =
    "bc84542f4bc0d900043c734b883eafd51fab91e634e6cb27be036da84505da2701ddf842c5dc37d9d13df0457e35"
    "6951e9e3a1e3dfb7abcb7f501ac52454438e8bf38b84c7b180f7e9f82cfb26c421024cb674856b5552c9e14a35a3"
    "c35d32cd5512c9cdbc977bafb06692d4298d9af55606e29d3b9774b8cd3a2732fee997e240ddfc0ec599cc07b2c1"
    "d9e6eb605a05d11cb85a87cfba9f4f157cb646840ddffd5e07ee24172f9c3eb6959d2e61ba43a7bc2fbc52489ff9"
    "fad09b26cdda39b4c9175d77d0539b2b3ee9dd0691b76401f463a593002283eb67d6fed14b76172b13c7c813dbff"
    "23c8254cf368a34165099b934de13a31d416c248e0568113148b";
const std::string kServerSignature =
    "16473ab622628535ee8c0864c1969307527c309b099bf17dffff55c4444a28aa8d611b847427425ff884cde4f793"
    "d0b2ae226d90ca81efe7273a079cddec0d48eca3cd33ff04fe957145c93dd9450d224c763b0b548b4bd764112ea2"
    "2eea261ee0dd58633be46164f1daecff4827c1f7c11b5d248c93d97ee9eb65a14ce87b0fc1032d34e01c3f49916f"
    "b3c62290d16aaac0c4f79a944a3b433a0139c027160470365ce040494aff0ad0ffde882fb5ca13baa7c468f48e72"
    "f3175ff75f19d26461bb314e50cff88d147c33770854633b6d914e597d298dbcb39740464be63db36b0c4c19eb61"
    "8aa5570f9b373e1cd95e89d534a5f38629bc8f9f89de9a0ed6c0";

TBytes Hex(const std::string& hex) {
    return SpotifyProtocol::FromHex(hex).value_or(TBytes{});
}

TBytes StripLeadingZeros(TBytes bytes) {
    const auto first = std::find_if(bytes.begin(), bytes.end(), [](std::uint8_t byte) { return byte != 0; });
    bytes.erase(bytes.begin(), first);
    return bytes;
}

ByteView FieldOf(ByteView message, int field) {
    ProtoReader reader(message);
    while (reader.next()) {
        if (reader.field() == field) {
            return reader.bytes();
        }
    }
    return {};
}

std::uint64_t VarintOf(ByteView message, int field) {
    ProtoReader reader(message);
    while (reader.next()) {
        if (reader.field() == field) {
            return reader.varint();
        }
    }
    return 0;
}

std::string TextOf(ByteView bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data), bytes.size);
}

struct Packet {
    std::uint8_t command = 0;
    TBytes payload;
};

class FakeAccessPoint {
public:
    void receive(ByteView bytes) {
        incoming.insert(incoming.end(), bytes.data, bytes.data + bytes.size);
        while (step()) {
        }
    }

    TBytes takeOutgoing() { return std::exchange(outgoing, {}); }

    void send(std::uint8_t command, ByteView payload) {
        TBytes packet{command, static_cast<std::uint8_t>(payload.size >> 8), static_cast<std::uint8_t>(payload.size)};
        packet.insert(packet.end(), payload.data, payload.data + payload.size);
        sendCipher->nonce(sendNonce++);
        sendCipher->encrypt(packet.data(), packet.size());
        std::uint8_t mac[4];
        sendCipher->finish(mac, sizeof mac);
        packet.insert(packet.end(), mac, mac + sizeof mac);
        outgoing.insert(outgoing.end(), packet.begin(), packet.end());
    }

    void welcome(const std::string& username, ByteView blob) {
        ProtoWriter welcome;
        welcome.string(10, username).varint(20, 1).varint(25, 1).varint(30, 1).bytes(40, blob);
        send(0xac, welcome.data());
    }

    std::string gc;
    bool challengeAnswered = false;
    std::vector<Packet> packets;

private:
    bool step() {
        switch (stage) {
            case Stage::Hello: return readHello();
            case Stage::Answer: return readAnswer();
            case Stage::Packets: return readPacket();
        }
        return false;
    }

    bool readHello() {
        if (incoming.size() < 6) {
            return false;
        }
        const std::uint32_t size = SpotifyProtocol::ReadU32(incoming.data() + 2);
        if (incoming.size() < size) {
            return false;
        }
        transcript.assign(incoming.begin(), incoming.begin() + size);
        const ByteView hello(incoming.data() + 6, size - 6);
        const TBytes clientKey = FieldOf(FieldOf(FieldOf(hello, 50), 10), 10).copy();
        gc = SpotifyProtocol::ToHex(clientKey);
        incoming.erase(incoming.begin(), incoming.begin() + size);

        ProtoWriter dh;
        dh.bytes(10, Hex(kServerPublic)).varint(20, 1).bytes(30, Hex(kServerSignature));
        ProtoWriter crypto;
        crypto.message(10, dh);
        ProtoWriter challenge;
        challenge.message(10, crypto);
        ProtoWriter response;
        response.message(10, challenge);
        TBytes reply;
        SpotifyProtocol::AppendU32(reply, static_cast<std::uint32_t>(4 + response.data().size()));
        reply.insert(reply.end(), response.data().begin(), response.data().end());
        transcript.insert(transcript.end(), reply.begin(), reply.end());
        outgoing.insert(outgoing.end(), reply.begin(), reply.end());

        const std::optional<TBytes> shared = SpotifyProtocol::ModPow(clientKey, Hex(kServerPrivate), Hex(kDhPrime));
        const TBytes secret = StripLeadingZeros(shared.value_or(TBytes{}));
        for (std::uint8_t i = 1; i < 6; ++i) {
            const SpotifyProtocol::TSha1 part = SpotifyProtocol::HmacSha1(secret, {transcript, {&i, 1}});
            material.insert(material.end(), part.begin(), part.end());
        }
        receiveCipher.emplace(ByteView{material.data() + 20, 32});
        sendCipher.emplace(ByteView{material.data() + 52, 32});
        stage = Stage::Answer;
        return true;
    }

    bool readAnswer() {
        if (incoming.size() < 4) {
            return false;
        }
        const std::uint32_t size = SpotifyProtocol::ReadU32(incoming.data());
        if (incoming.size() < size) {
            return false;
        }
        const ByteView answer(incoming.data() + 4, size - 4);
        const TBytes hmac = FieldOf(FieldOf(FieldOf(answer, 10), 10), 10).copy();
        const SpotifyProtocol::TSha1 expected = SpotifyProtocol::HmacSha1({material.data(), 20}, {transcript});
        challengeAnswered = hmac == TBytes(expected.begin(), expected.end());
        incoming.erase(incoming.begin(), incoming.begin() + size);
        stage = Stage::Packets;
        return true;
    }

    bool readPacket() {
        if (incoming.size() < 3 + 4) {
            return false;
        }
        TBytes header(incoming.begin(), incoming.begin() + 3);
        SpotifyProtocol::Shannon probe = *receiveCipher;
        probe.nonce(receiveNonce);
        probe.decrypt(header.data(), header.size());
        const std::size_t size = SpotifyProtocol::ReadU16(header.data() + 1);
        if (incoming.size() < 3 + size + 4) {
            return false;
        }
        TBytes packet(incoming.begin(), incoming.begin() + 3 + static_cast<std::ptrdiff_t>(size));
        receiveCipher->nonce(receiveNonce++);
        receiveCipher->decrypt(packet.data(), packet.size());
        std::uint8_t mac[4];
        receiveCipher->finish(mac, sizeof mac);
        const bool authentic = std::equal(mac, mac + 4, incoming.begin() + 3 + static_cast<std::ptrdiff_t>(size));
        incoming.erase(incoming.begin(), incoming.begin() + 3 + static_cast<std::ptrdiff_t>(size) + 4);
        if (authentic) {
            packets.push_back({packet[0], TBytes(packet.begin() + 3, packet.end())});
        }
        return true;
    }

    enum class Stage { Hello, Answer, Packets };
    Stage stage = Stage::Hello;
    TBytes incoming;
    TBytes outgoing;
    TBytes transcript;
    TBytes material;
    std::optional<SpotifyProtocol::Shannon> sendCipher;
    std::optional<SpotifyProtocol::Shannon> receiveCipher;
    std::uint32_t sendNonce = 0;
    std::uint32_t receiveNonce = 0;
};

void Counting(std::uint8_t* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        data[i] = static_cast<std::uint8_t>(i * 13 + 5);
    }
}

SpotifyProtocol::ClientIdentity Identity() {
    SpotifyProtocol::ClientIdentity identity;
    identity.systemInformation = "QiYaa 0.7.2";
    identity.versionString = "QiYaa 0.7.2";
    identity.deviceId = std::string(40, 'd');
    return identity;
}

SpotifyProtocol::Credentials Stored() {
    return SpotifyProtocol::Credentials{
        .username = "owner",
        .type = SpotifyProtocol::AuthenticationType::StoredCredentials,
        .data = TBytes{1, 2, 3, 4},
    };
}

SpotifyProtocol::ServerKey TestKey() {
    return SpotifyProtocol::ServerKey{.modulus = Hex(kTestKeyModulus), .exponent = 65537};
}

class Exchange {
public:
    explicit Exchange(SpotifyProtocol::Credentials credentials, SpotifyProtocol::ServerKey key = TestKey())
        : client(Identity(), std::move(credentials), Counting, std::move(key)) { }

    void start() {
        client.start();
        pump(0);
    }

    void pump(std::int64_t nowMs, bool byteByByte = false) {
        for (int round = 0; round < 8; ++round) {
            const TBytes toServer = client.takeOutgoing();
            if (!toServer.empty()) {
                server.receive(toServer);
            }
            const TBytes toClient = server.takeOutgoing();
            if (byteByByte) {
                for (const std::uint8_t byte : toClient) {
                    client.receive({&byte, 1}, nowMs);
                }
            } else if (!toClient.empty()) {
                client.receive(toClient, nowMs);
            }
            if (toServer.empty() && toClient.empty()) {
                return;
            }
        }
    }

    template <typename Event>
    std::vector<Event> eventsOf() {
        std::vector<Event> found;
        for (SpotifyProtocol::TApEvent& event : client.takeEvents()) {
            if (auto* wanted = std::get_if<Event>(&event)) {
                found.push_back(std::move(*wanted));
            }
        }
        return found;
    }

    std::optional<Packet> lastPacket(std::uint8_t command) const {
        for (auto it = server.packets.rbegin(); it != server.packets.rend(); ++it) {
            if (it->command == command) {
                return *it;
            }
        }
        return std::nullopt;
    }

    SpotifyProtocol::AccessPoint client;
    FakeAccessPoint server;
};

}  // namespace

class TestSpotifyAccessPoint : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void logsInWithStoredCredentialsAndGetsAKey() {  // SPOT-04, SPOT-13
        Exchange exchange(Stored());
        exchange.start();
        QVERIFY(exchange.server.challengeAnswered);
        const std::optional<Packet> login = exchange.lastPacket(0xab);
        QVERIFY(login);
        const ByteView credentials = FieldOf(login->payload, 10);
        QCOMPARE(TextOf(FieldOf(credentials, 10)), std::string("owner"));
        QCOMPARE(VarintOf(credentials, 20), std::uint64_t{1});
        QCOMPARE(FieldOf(credentials, 30).copy(), (TBytes{1, 2, 3, 4}));
        QCOMPARE(TextOf(FieldOf(login->payload, 70)), std::string("QiYaa 0.7.2"));
        QCOMPARE(TextOf(FieldOf(FieldOf(login->payload, 50), 100)), std::string(40, 'd'));

        exchange.server.welcome("owner", TBytes{9, 9, 9});
        exchange.server.send(0x1b, ByteView::Of("BY"));
        exchange.server.send(0x50, ByteView::Of("<products><product><type>premium</type></product></products>"));
        exchange.pump(1000);
        QVERIFY(exchange.client.loggedIn());
        const std::vector<SpotifyProtocol::TApEvent> events = exchange.client.takeEvents();
        QCOMPARE(events.size(), size_t{3});
        const auto* welcome = std::get_if<SpotifyProtocol::ApLoggedIn>(&events[0]);
        QVERIFY(welcome);
        QCOMPARE(welcome->reusable.username, std::string("owner"));
        QCOMPARE(welcome->reusable.data, (TBytes{9, 9, 9}));
        QCOMPARE(std::get<SpotifyProtocol::ApCountry>(events[1]).code, std::string("BY"));
        QCOMPARE(std::get<SpotifyProtocol::ApProduct>(events[2]).type, std::string("premium"));

        const SpotifyProtocol::TGid track{1};
        const SpotifyProtocol::TFileId file{2};
        const std::optional<std::uint32_t> sequence = exchange.client.requestAudioKey(track, file);
        QVERIFY(sequence);
        exchange.pump(1000);
        const std::optional<Packet> request = exchange.lastPacket(0x0c);
        QVERIFY(request);
        QCOMPARE(request->payload.size(), size_t{20 + 16 + 4 + 2});
        TBytes reply;
        SpotifyProtocol::AppendU32(reply, *sequence);
        reply.insert(reply.end(), 16, 0x77);
        exchange.server.send(0x0d, reply);
        exchange.pump(1000);
        const auto keys = exchange.eventsOf<SpotifyProtocol::ApAudioKey>();
        QCOMPARE(keys.size(), size_t{1});
        QCOMPARE(keys[0].sequence, *sequence);
        QCOMPARE(keys[0].key[15], std::uint8_t{0x77});
    }

    void aTokenLoginSendsTheTokenAsASpotifyToken() {  // SPOT-03
        Exchange exchange(SpotifyProtocol::Credentials::FromAccessToken("BQ-token"));
        exchange.start();
        const std::optional<Packet> login = exchange.lastPacket(0xab);
        QVERIFY(login);
        const ByteView credentials = FieldOf(login->payload, 10);
        QCOMPARE(FieldOf(credentials, 10).size, size_t{0});
        QCOMPARE(VarintOf(credentials, 20), std::uint64_t{3});
        QCOMPARE(TextOf(FieldOf(credentials, 30)), std::string("BQ-token"));
    }

    void aRefusedKeyCarriesItsCode() {  // SPOT-16, SPOT-17
        Exchange exchange(Stored());
        exchange.start();
        exchange.server.welcome("owner", TBytes{9});
        exchange.pump(0);
        exchange.client.takeEvents();
        const std::optional<std::uint32_t> sequence =
            exchange.client.requestAudioKey(SpotifyProtocol::TGid{}, SpotifyProtocol::TFileId{});
        QVERIFY(sequence);
        TBytes refusal;
        SpotifyProtocol::AppendU32(refusal, *sequence);
        SpotifyProtocol::AppendU16(refusal, 1);
        exchange.server.send(0x0e, refusal);
        exchange.pump(0);
        const auto refused = exchange.eventsOf<SpotifyProtocol::ApAudioKeyRefused>();
        QCOMPARE(refused.size(), size_t{1});
        QCOMPARE(refused[0].sequence, *sequence);
        QCOMPARE(refused[0].code, std::uint16_t{1});
        QVERIFY(exchange.client.loggedIn());
    }

    void aRefusedLoginCarriesItsCode() {  // SPOT-04, SPOT-05
        Exchange exchange(Stored());
        exchange.start();
        ProtoWriter failure;
        failure.varint(10, 12).string(40, "bad credentials");
        exchange.server.send(0xad, failure.data());
        exchange.pump(0);
        const auto failed = exchange.eventsOf<SpotifyProtocol::ApLoginFailed>();
        QCOMPARE(failed.size(), size_t{1});
        QCOMPARE(failed[0].code, SpotifyProtocol::LoginError::BadCredentials);
        QVERIFY(exchange.client.failed());
        QVERIFY(!exchange.client.requestAudioKey(SpotifyProtocol::TGid{}, SpotifyProtocol::TFileId{}));
    }

    void anAccessPointNotSignedBySpotifyIsRefused() {
        Exchange exchange(Stored(), SpotifyProtocol::SpotifyServerKey());
        exchange.start();
        QVERIFY(exchange.client.failed());
        QVERIFY(!exchange.lastPacket(0xab));
        QCOMPARE(exchange.eventsOf<SpotifyProtocol::ApFailure>().size(), size_t{1});
    }

    void packetsSplitIntoSingleBytesStillArrive() {
        Exchange exchange(Stored());
        exchange.start();
        exchange.server.welcome("owner", TBytes{9});
        exchange.server.send(0x1b, ByteView::Of("BY"));
        exchange.pump(0, true);
        QVERIFY(exchange.client.loggedIn());
        QCOMPARE(exchange.eventsOf<SpotifyProtocol::ApCountry>().size(), size_t{1});
    }

    void keepAliveAnswersAPingAfterAMinuteAndFailsWithoutOne() {  // SPOT-18
        using SpotifyProtocol::AccessPoint;
        Exchange exchange(Stored());
        exchange.start();
        QVERIFY(!exchange.client.deadline());
        exchange.server.welcome("owner", TBytes{9});
        exchange.pump(0);
        QCOMPARE(exchange.client.deadline(), std::optional<std::int64_t>{AccessPoint::kFirstPingMs});

        exchange.server.send(0x04, TBytes{0, 0, 0, 1});
        exchange.pump(5000);
        QCOMPARE(exchange.client.deadline(), std::optional<std::int64_t>{5000 + AccessPoint::kPongDelayMs});
        exchange.client.advance(5000 + AccessPoint::kPongDelayMs - 1);
        exchange.pump(5000 + AccessPoint::kPongDelayMs - 1);
        QVERIFY(!exchange.lastPacket(0x49));
        const std::int64_t pongAt = 5000 + AccessPoint::kPongDelayMs;
        exchange.client.advance(pongAt);
        exchange.pump(pongAt);
        QVERIFY(exchange.lastPacket(0x49));
        QCOMPARE(exchange.client.deadline(), std::optional<std::int64_t>{pongAt + AccessPoint::kPongAckMs});

        exchange.server.send(0x4a, TBytes{});
        exchange.pump(pongAt + 100);
        const std::int64_t silentUntil = pongAt + 100 + AccessPoint::kNextPingMs;
        QCOMPARE(exchange.client.deadline(), std::optional<std::int64_t>{silentUntil});
        exchange.client.advance(silentUntil);
        QVERIFY(exchange.client.failed());
        QCOMPARE(exchange.eventsOf<SpotifyProtocol::ApFailure>().size(), size_t{1});
    }
};

QTEST_GUILESS_MAIN(TestSpotifyAccessPoint)
#include "spotify_access_point_test.moc"
