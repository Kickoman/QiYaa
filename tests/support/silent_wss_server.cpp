#include "support/silent_wss_server.h"

#include <QCryptographicHash>
#include <QFile>
#include <QHostAddress>
#include <QRegularExpression>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>

namespace Tests {

namespace {

QByteArray ReadData(const QString& name) {
    QFile file(QStringLiteral(QIYAA_TEST_DATA "/") + name);
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("cannot read %s", qUtf8Printable(file.fileName()));
    }
    return file.readAll();
}

QByteArray UpgradeAnswer(const QByteArray& request) {
    static const QRegularExpression keyLine(
        QStringLiteral("^Sec-WebSocket-Key:\\s*(\\S+)\\s*$"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption
    );
    const QByteArray key = keyLine.match(QString::fromLatin1(request)).captured(1).toLatin1();
    const QByteArray accept =
        QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64();
    return "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "
        + accept + "\r\n\r\n";
}

}  // namespace

SilentWssServer::SilentWssServer(Answers whatItAnswers)
    : answers(whatItAnswers)
    , previousDefault(QSslConfiguration::defaultConfiguration()) {
    QSslConfiguration anyServer = previousDefault;
    anyServer.setPeerVerifyMode(QSslSocket::VerifyNone);
    QSslConfiguration::setDefaultConfiguration(anyServer);

    QSslConfiguration own;
    own.setLocalCertificate(QSslCertificate(ReadData(QStringLiteral("localhost.crt"))));
    own.setPrivateKey(QSslKey(ReadData(QStringLiteral("localhost.key")), QSsl::Rsa));
    server.setSslConfiguration(own);
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        qFatal("the silent wss server cannot listen");
    }
    connect(&server, &QSslServer::errorOccurred, this, [](QSslSocket*, QAbstractSocket::SocketError error) {
        qWarning("the silent wss server: socket error %d", static_cast<int>(error));
    });
    connect(&server, &QTcpServer::pendingConnectionAvailable, this, [this] {
        while (auto* socket = qobject_cast<QSslSocket*>(server.nextPendingConnection())) {
            ++connectionCount;
            connect(socket, &QIODevice::readyRead, this, [this, socket] { readRequest(socket); });
            connect(socket, &QAbstractSocket::disconnected, this, [this, socket] {
                requests.remove(socket);
                socket->deleteLater();
            });
            readRequest(socket);
        }
    });
}

SilentWssServer::~SilentWssServer() {
    QSslConfiguration::setDefaultConfiguration(previousDefault);
}

QUrl SilentWssServer::url() const {
    return QUrl(QStringLiteral("wss://127.0.0.1:%1/ws").arg(server.serverPort()));
}

void SilentWssServer::readRequest(QSslSocket* socket) {
    QByteArray& request = requests[socket];
    if (request.endsWith("\r\n\r\n")) {
        socket->readAll();
        return;
    }
    request += socket->readAll();
    if (!request.contains("\r\n\r\n") || answers == Answers::Nothing) {
        return;
    }
    ++upgradeCount;
    socket->write(UpgradeAnswer(request));
}

}  // namespace Tests
