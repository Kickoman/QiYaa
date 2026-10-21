#include "spotify/protocol/access_point.h"
#include "spotify/protocol/bytes.h"
#include "spotify/protocol/crypto.h"
#include "spotify/protocol/protobuf.h"
#include "spotify/protocol/shannon.h"
#include "support/fake_access_point.h"

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
using SpotifyProtocol::ProtoWriter;
using SpotifyProtocol::TBytes;
using Tests::FieldOf;
using Tests::TextOf;
using Tests::VarintOf;

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

class Exchange {
public:
    explicit Exchange(
        SpotifyProtocol::Credentials credentials,
        SpotifyProtocol::ServerKey key = Tests::FakeAccessPointKey()
    )
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

    std::optional<Tests::SpotifyPacket> lastPacket(std::uint8_t command) const {
        for (auto it = server.packets.rbegin(); it != server.packets.rend(); ++it) {
            if (it->command == command) {
                return *it;
            }
        }
        return std::nullopt;
    }

    SpotifyProtocol::AccessPoint client;
    Tests::FakeAccessPoint server;
};

}  // namespace

class TestSpotifyAccessPoint : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void logsInWithStoredCredentialsAndGetsAKey() {  // SPOT-04, SPOT-13
        Exchange exchange(Stored());
        exchange.start();
        QVERIFY(exchange.server.challengeAnswered);
        const std::optional<Tests::SpotifyPacket> login = exchange.lastPacket(0xab);
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
        const std::optional<Tests::SpotifyPacket> request = exchange.lastPacket(0x0c);
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
        const std::optional<Tests::SpotifyPacket> login = exchange.lastPacket(0xab);
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
