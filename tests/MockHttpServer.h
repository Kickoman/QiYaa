// A tiny HTTP/1.1 server for tests: canned responses per "METHOD /path",
// records every request. One request per connection (Connection: close).
#pragma once

#include <functional>

#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QPointer>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

struct MockRequest {
    QByteArray method;
    QString path;    // without query
    QUrlQuery query;
    QByteArray body;
    QHash<QByteArray, QByteArray> headers;  // lower-case names

    QUrlQuery form() const { return QUrlQuery(QString::fromUtf8(body).replace(u'+', u' ')); }
    QString formValue(const QString& key) const { return form().queryItemValue(key, QUrl::FullyDecoded); }
};

struct MockResponse {
    int status = 200;
    QByteArray body;
    int delayMs = 0;
};

class MockHttpServer : public QObject {
public:
    using Handler = std::function<MockResponse(const MockRequest&)>;

    MockHttpServer() {
        m_server.listen(QHostAddress::LocalHost);
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = m_server.nextPendingConnection()) {
                auto buffer = std::make_shared<QByteArray>();
                QObject::connect(s, &QTcpSocket::readyRead, s, [this, s, buffer] {
                    *buffer += s->readAll();
                    handle(s, *buffer);
                });
                QObject::connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }

    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }

    // Exact "METHOD /path" match.
    void on(const QByteArray& method, const QString& path, Handler h) { m_routes[method + ' ' + path.toUtf8()] = std::move(h); }
    void json(const QByteArray& method, const QString& path, const QByteArray& body, int status = 200) {
        on(method, path, [body, status](const MockRequest&) { return MockResponse{status, body}; });
    }
    // Wraps `result` in Yandex's {"result": ...} envelope.
    void result(const QByteArray& method, const QString& path, const QByteArray& resultJson) {
        json(method, path, "{\"invocationInfo\":{},\"result\":" + resultJson + "}");
    }

    const QList<MockRequest>& requests() const { return m_requests; }
    const MockRequest* last(const QString& path) const {
        for (auto it = m_requests.crbegin(); it != m_requests.crend(); ++it)
            if (it->path == path) return &*it;
        return nullptr;
    }

private:
    void handle(QTcpSocket* s, QByteArray& buf) {
        const int headerEnd = buf.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const QList<QByteArray> lines = buf.left(headerEnd).split('\n');
        MockRequest req;
        const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
        req.method = first.value(0);
        const QUrl url(QString::fromUtf8(first.value(1)));
        req.path = url.path();
        req.query = QUrlQuery(url);
        qsizetype contentLength = 0;
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines[i].trimmed();
            const int colon = line.indexOf(':');
            if (colon <= 0) continue;
            const QByteArray name = line.left(colon).trimmed().toLower();
            req.headers[name] = line.mid(colon + 1).trimmed();
            if (name == "content-length") contentLength = line.mid(colon + 1).trimmed().toLongLong();
        }
        if (buf.size() < headerEnd + 4 + contentLength) return;  // wait for the body
        req.body = buf.mid(headerEnd + 4, contentLength);
        buf.clear();
        m_requests << req;

        const auto it = m_routes.constFind(req.method + ' ' + req.path.toUtf8());
        const MockResponse resp = it != m_routes.cend() ? (*it)(req) : MockResponse{404, "{\"error\":\"not found\"}"};
        QByteArray out = "HTTP/1.1 " + QByteArray::number(resp.status) + " X\r\n";
        out += "Content-Type: application/json\r\nConnection: close\r\n";
        out += "Content-Length: " + QByteArray::number(resp.body.size()) + "\r\n\r\n" + resp.body;
        QPointer<QTcpSocket> guard(s);
        QTimer::singleShot(resp.delayMs, s, [guard, out] {
            if (!guard) return;
            guard->write(out);
            guard->disconnectFromHost();
        });
    }

    QTcpServer m_server;
    QHash<QByteArray, Handler> m_routes;
    QList<MockRequest> m_requests;
};
