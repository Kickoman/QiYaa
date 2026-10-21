#pragma once

#include "spotify/protocol/access_point.h"
#include "spotify/protocol/bytes.h"
#include "spotify/protocol/shannon.h"

#include <QObject>
#include <QString>
#include <QTcpServer>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QTcpSocket;

namespace Tests {

SpotifyProtocol::ServerKey FakeAccessPointKey();

SpotifyProtocol::ByteView FieldOf(SpotifyProtocol::ByteView message, int field);
std::uint64_t VarintOf(SpotifyProtocol::ByteView message, int field);
std::string TextOf(SpotifyProtocol::ByteView bytes);

struct SpotifyPacket {
    std::uint8_t command = 0;
    SpotifyProtocol::TBytes payload;
};

class FakeAccessPoint {
public:
    void receive(SpotifyProtocol::ByteView bytes);
    SpotifyProtocol::TBytes takeOutgoing();
    void send(std::uint8_t command, SpotifyProtocol::ByteView payload);
    void welcome(const std::string& username, SpotifyProtocol::ByteView blob);

    std::string gc;
    bool challengeAnswered = false;
    std::vector<SpotifyPacket> packets;

private:
    bool step();
    bool readHello();
    bool readAnswer();
    bool readPacket();

    enum class Stage { Hello, Answer, Packets };
    Stage stage = Stage::Hello;
    SpotifyProtocol::TBytes incoming;
    SpotifyProtocol::TBytes outgoing;
    SpotifyProtocol::TBytes transcript;
    SpotifyProtocol::TBytes material;
    std::optional<SpotifyProtocol::Shannon> sendCipher;
    std::optional<SpotifyProtocol::Shannon> receiveCipher;
    std::uint32_t sendNonce = 0;
    std::uint32_t receiveNonce = 0;
};

class FakeAccessPointServer : public QObject {
public:
    FakeAccessPointServer();
    ~FakeAccessPointServer() override;

    QString address() const;
    int connectionCount() const { return accepted; }
    void dropConnections();
    void ping();

    SpotifyProtocol::TBytes welcomeBlob = {9, 9, 9};
    std::string country = "BY";
    std::string product = "premium";
    int loginRefusal = -1;
    SpotifyProtocol::TAesKey key{};
    std::function<std::optional<std::uint16_t>(int attempt)> keyRefusal;
    bool answerKeys = true;
    std::vector<SpotifyPacket> packets;

private:
    struct Connection;

    void accept();
    void handle(Connection& connection);

    QTcpServer server;
    std::vector<std::unique_ptr<Connection>> connections;
    int accepted = 0;
    int keyRequests = 0;
};

}  // namespace Tests
