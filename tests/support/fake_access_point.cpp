#include "support/fake_access_point.h"

#include "spotify/protocol/crypto.h"
#include "spotify/protocol/protobuf.h"

#include <QHostAddress>
#include <QTcpSocket>

#include <algorithm>
#include <utility>

namespace Tests {

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

}  // namespace

SpotifyProtocol::ServerKey FakeAccessPointKey() {
    return SpotifyProtocol::ServerKey{.modulus = Hex(kTestKeyModulus), .exponent = 65537};
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

void FakeAccessPoint::receive(ByteView bytes) {
    incoming.insert(incoming.end(), bytes.data, bytes.data + bytes.size);
    while (step()) {
    }
}

TBytes FakeAccessPoint::takeOutgoing() {
    return std::exchange(outgoing, {});
}

void FakeAccessPoint::send(std::uint8_t command, ByteView payload) {
    TBytes packet{command, static_cast<std::uint8_t>(payload.size >> 8), static_cast<std::uint8_t>(payload.size)};
    packet.insert(packet.end(), payload.data, payload.data + payload.size);
    sendCipher->nonce(sendNonce++);
    sendCipher->encrypt(packet.data(), packet.size());
    std::uint8_t mac[4];
    sendCipher->finish(mac, sizeof mac);
    packet.insert(packet.end(), mac, mac + sizeof mac);
    outgoing.insert(outgoing.end(), packet.begin(), packet.end());
}

void FakeAccessPoint::welcome(const std::string& username, ByteView blob) {
    ProtoWriter message;
    message.string(10, username).varint(20, 1).varint(25, 1).varint(30, 1).bytes(40, blob);
    send(0xac, message.data());
}

bool FakeAccessPoint::step() {
    switch (stage) {
        case Stage::Hello: return readHello();
        case Stage::Answer: return readAnswer();
        case Stage::Packets: return readPacket();
    }
    return false;
}

bool FakeAccessPoint::readHello() {
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

bool FakeAccessPoint::readAnswer() {
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

bool FakeAccessPoint::readPacket() {
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
    const auto end = static_cast<std::ptrdiff_t>(3 + size);
    TBytes packet(incoming.begin(), incoming.begin() + end);
    receiveCipher->nonce(receiveNonce++);
    receiveCipher->decrypt(packet.data(), packet.size());
    std::uint8_t mac[4];
    receiveCipher->finish(mac, sizeof mac);
    const bool authentic = std::equal(mac, mac + 4, incoming.begin() + end);
    incoming.erase(incoming.begin(), incoming.begin() + end + 4);
    if (authentic) {
        packets.push_back({packet[0], TBytes(packet.begin() + 3, packet.end())});
    }
    return true;
}

struct FakeAccessPointServer::Connection {
    QTcpSocket* socket = nullptr;
    FakeAccessPoint accessPoint;
    std::size_t handled = 0;
};

FakeAccessPointServer::FakeAccessPointServer() {
    server.listen(QHostAddress::LocalHost, 0);
    connect(&server, &QTcpServer::newConnection, this, &FakeAccessPointServer::accept);
}

FakeAccessPointServer::~FakeAccessPointServer() {
    dropConnections();
}

QString FakeAccessPointServer::address() const {
    return QStringLiteral("127.0.0.1:%1").arg(server.serverPort());
}

void FakeAccessPointServer::dropConnections() {
    for (const std::unique_ptr<Connection>& connection : connections) {
        if (connection->socket) {
            connection->socket->disconnect(this);
            connection->socket->abort();
            connection->socket->deleteLater();
            connection->socket = nullptr;
        }
    }
    connections.clear();
}

void FakeAccessPointServer::ping() {
    for (const std::unique_ptr<Connection>& connection : connections) {
        if (connection->socket) {
            connection->accessPoint.send(0x04, TBytes{0, 0, 0, 1});
            const TBytes out = connection->accessPoint.takeOutgoing();
            connection->socket->write(reinterpret_cast<const char*>(out.data()), static_cast<qint64>(out.size()));
        }
    }
}

void FakeAccessPointServer::accept() {
    while (QTcpSocket* socket = server.nextPendingConnection()) {
        ++accepted;
        auto connection = std::make_unique<Connection>();
        connection->socket = socket;
        Connection* raw = connection.get();
        connections.push_back(std::move(connection));
        connect(socket, &QTcpSocket::readyRead, this, [this, raw] { handle(*raw); });
    }
}

void FakeAccessPointServer::handle(Connection& connection) {
    const QByteArray bytes = connection.socket->readAll();
    connection.accessPoint.receive(
        {reinterpret_cast<const std::uint8_t*>(bytes.constData()), static_cast<std::size_t>(bytes.size())}
    );
    std::vector<SpotifyPacket>& seen = connection.accessPoint.packets;
    for (; connection.handled < seen.size(); ++connection.handled) {
        const SpotifyPacket packet = seen[connection.handled];
        packets.push_back(packet);
        if (packet.command == 0xab) {
            if (loginRefusal >= 0) {
                ProtoWriter failure;
                failure.varint(10, static_cast<std::uint64_t>(loginRefusal));
                connection.accessPoint.send(0xad, failure.data());
                continue;
            }
            const ByteView credentials = FieldOf(packet.payload, 10);
            std::string username = TextOf(FieldOf(credentials, 10));
            if (username.empty()) {
                username = "owner";
            }
            connection.accessPoint.welcome(username, welcomeBlob);
            connection.accessPoint.send(0x1b, ByteView::Of(country));
            const std::string info = "<products><product><type>" + product + "</type></product></products>";
            connection.accessPoint.send(0x50, ByteView::Of(info));
        } else if (packet.command == 0x0c && answerKeys && packet.payload.size() >= 40) {
            ++keyRequests;
            TBytes reply(packet.payload.begin() + 36, packet.payload.begin() + 40);
            const std::optional<std::uint16_t> refusal = keyRefusal ? keyRefusal(keyRequests) : std::nullopt;
            if (refusal) {
                SpotifyProtocol::AppendU16(reply, *refusal);
                connection.accessPoint.send(0x0e, reply);
            } else {
                reply.insert(reply.end(), key.begin(), key.end());
                connection.accessPoint.send(0x0d, reply);
            }
        }
    }
    const TBytes out = connection.accessPoint.takeOutgoing();
    if (!out.empty()) {
        connection.socket->write(reinterpret_cast<const char*>(out.data()), static_cast<qint64>(out.size()));
    }
}

}  // namespace Tests
