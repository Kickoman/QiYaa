#include "support/mock_http_server.h"

#include "support/spec_fixtures.h"

#include <QHostAddress>
#include <QLatin1String>
#include <QNetworkRequest>
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

void MockHttpServer::fixture(
    const QByteArray& method,
    const QString& path,
    const QString& name,
    int delayMs
) {
    const MockResponse response{FixtureStatus(name), Fixture(name), delayMs};
    on(method, path, [response](const MockRequest&) { return response; });
}

void MockHttpServer::audioTracks(const QStringList& trackIds, const QByteArray& mp3) {
    for (const QString& id : trackIds) {
        result(
            "GET", QStringLiteral("/tracks/%1/download-info").arg(id),
            "[{\"codec\":\"mp3\",\"bitrateInKbps\":320,\"downloadInfoUrl\":\"" + baseUrl().toUtf8()
                + "/dlinfo" + id.toUtf8() + "\"}]"
        );
        json(
            "GET", QStringLiteral("/dlinfo%1").arg(id),
            "{\"host\":\"" + QUrl(baseUrl()).authority().toUtf8() + "\",\"path\":\"/t" + id.toUtf8()
                + "\",\"ts\":\"1\",\"s\":\"s\"}"
        );
    }
    onPrefix("GET", "/get-mp3/", [mp3](const MockRequest&) { return MockResponse{200, mp3}; });
    fixture("POST", "/play-audio", "play-audio/ok");
}

QNetworkReply* LocalNetworkAccessManager::createRequest(
    Operation op,
    const QNetworkRequest& request,
    QIODevice* outgoingData
) {
    QNetworkRequest localRequest(request);
    QUrl url = localRequest.url();
    if (url.scheme() == QLatin1String("https") && url.host() == QLatin1String("127.0.0.1")) {
        url.setScheme(QStringLiteral("http"));
        localRequest.setUrl(url);
    }
    return QNetworkAccessManager::createRequest(op, localRequest, outgoingData);
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
    QByteArray reply = "HTTP/1.1 " + QByteArray::number(response.status) + " X\r\n";
    reply += "Content-Type: application/json\r\nConnection: close\r\n";
    reply +=
        "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n\r\n" + response.body;
    QPointer<QTcpSocket> guard(socket);
    QTimer::singleShot(response.delayMs, socket, [guard, reply] {
        if (!guard) {
            return;
        }
        guard->write(reply);
        guard->disconnectFromHost();
    });
}

}  // namespace Tests
