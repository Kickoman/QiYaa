#include "support/mock_http_server.h"

#include <QHostAddress>
#include <QPointer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <utility>

namespace Tests {

QUrlQuery MockRequest::form() const {
    return QUrlQuery(QString::fromUtf8(body).replace(u'+', u' '));
}

QString MockRequest::formValue(const QString& key) const {
    return form().queryItemValue(key, QUrl::FullyDecoded);
}

MockHttpServer::MockHttpServer() {
    server.listen(QHostAddress::LocalHost);
    QObject::connect(&server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket* socket = server.nextPendingConnection()) {
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
                *buffer += socket->readAll();
                handle(socket, *buffer);
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
}

QString MockHttpServer::baseUrl() const {
    return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
}

void MockHttpServer::on(const QByteArray& method, const QString& path, THandler handler) {
    routes[method + ' ' + path.toUtf8()] = std::move(handler);
}

void MockHttpServer::onPrefix(const QByteArray& method, const QString& prefix, THandler handler) {
    prefixRoutes.append({method, prefix, std::move(handler)});
}

void MockHttpServer::json(
    const QByteArray& method,
    const QString& path,
    const QByteArray& body,
    int status
) {
    on(method, path, [body, status](const MockRequest&) { return MockResponse{status, body}; });
}

void MockHttpServer::result(
    const QByteArray& method,
    const QString& path,
    const QByteArray& resultJson
) {
    json(method, path, "{\"invocationInfo\":{},\"result\":" + resultJson + "}");
}

const MockRequest* MockHttpServer::last(const QString& path) const {
    for (auto it = recordedRequests.crbegin(); it != recordedRequests.crend(); ++it) {
        if (it->path == path) {
            return &*it;
        }
    }
    return nullptr;
}

void MockHttpServer::handle(QTcpSocket* socket, QByteArray& buffer) {
    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return;
    }
    const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
    MockRequest request;
    const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
    request.method = requestLine.value(0);
    const QUrl url(QString::fromUtf8(requestLine.value(1)));
    request.path = url.path();
    request.query = QUrlQuery(url);
    qsizetype contentLength = 0;
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines[i].trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0) {
            continue;
        }
        const QByteArray name = line.left(colon).trimmed().toLower();
        request.headers[name] = line.mid(colon + 1).trimmed();
        if (name == "content-length") {
            contentLength = line.mid(colon + 1).trimmed().toLongLong();
        }
    }
    if (buffer.size() < headerEnd + 4 + contentLength) {
        return;
    }
    request.body = buffer.mid(headerEnd + 4, contentLength);
    buffer.clear();
    recordedRequests << request;

    const auto it = routes.constFind(request.method + ' ' + request.path.toUtf8());
    MockResponse response{404, "{\"error\":\"not found\"}"};
    if (it != routes.cend()) {
        response = (*it)(request);
    } else {
        for (const PrefixRoute& route : prefixRoutes) {
            if (route.method == request.method && request.path.startsWith(route.prefix)) {
                response = route.handler(request);
                break;
            }
        }
    }
    QByteArray out = "HTTP/1.1 " + QByteArray::number(response.status) + " X\r\n";
    out += "Content-Type: application/json\r\nConnection: close\r\n";
    out +=
        "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n\r\n" + response.body;
    QPointer<QTcpSocket> guard(socket);
    QTimer::singleShot(response.delayMs, socket, [guard, out] {
        if (!guard) {
            return;
        }
        guard->write(out);
        guard->disconnectFromHost();
    });
}

}  // namespace Tests
