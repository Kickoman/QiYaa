#include "support/jam_stub_server.h"

#include "jam/codec.h"

#include <QHostAddress>
#include <QWebSocket>

#include <utility>

namespace Tests {

JamStubServer::JamStubServer()
    : server(QStringLiteral("jam-stub"), QWebSocketServer::NonSecureMode) {
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        qFatal("the stub server cannot listen");
    }
    connect(&server, &QWebSocketServer::newConnection, this, [this] {
        while (QWebSocket* socket = server.nextPendingConnection()) {
            connections.push_back(Connection{std::unique_ptr<QWebSocket>(socket), {}});
            connect(
                socket, &QWebSocket::textMessageReceived, this,
                [this, socket](const QString& text) {
                    for (Connection& each : connections) {
                        if (each.socket.get() == socket) {
                            each.received.append(text);
                        }
                    }
                }
            );
        }
    });
}

JamStubServer::~JamStubServer() = default;

QUrl JamStubServer::url() const {
    return QUrl(QStringLiteral("ws://127.0.0.1:%1/ws").arg(server.serverPort()));
}

QString JamStubServer::serverUrl() const {
    return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
}

QStringList JamStubServer::received() const {
    return connections.empty() ? QStringList() : connections.back().received;
}

std::vector<Jam::ClientMessage> JamStubServer::messages() const {
    std::vector<Jam::ClientMessage> decoded;
    for (const QString& text : received()) {
        const Jam::Decoded<Jam::ClientMessage> message = Jam::DecodeClient(text.toUtf8());
        if (message.message) {
            decoded.push_back(*message.message);
        }
    }
    return decoded;
}

void JamStubServer::send(const QByteArray& text) {
    last().sendTextMessage(QString::fromUtf8(text));
}

void JamStubServer::welcome(qint64 serverTime) {
    send(QStringLiteral(R"({"type":"welcome","protocol":1,"serverTime":%1})")
             .arg(serverTime)
             .toUtf8());
}

}  // namespace Tests
