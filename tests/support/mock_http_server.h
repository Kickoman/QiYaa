#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QUrlQuery>

#include <functional>

class QTcpSocket;

namespace Tests {

struct MockRequest {
    QByteArray method;
    QString path;
    QUrlQuery query;
    QByteArray body;
    QHash<QByteArray, QByteArray> headers;

    QUrlQuery form() const;
    QString formValue(const QString& key) const;
};

struct MockResponse {
    int status = 200;
    QByteArray body;
    int delayMs = 0;
};

class MockHttpServer : public QObject {
public:
    using THandler = std::function<MockResponse(const MockRequest&)>;

    MockHttpServer();

    QString baseUrl() const;

    void on(const QByteArray& method, const QString& path, THandler handler);
    void onPrefix(const QByteArray& method, const QString& prefix, THandler handler);
    void
    json(const QByteArray& method, const QString& path, const QByteArray& body, int status = 200);
    void result(const QByteArray& method, const QString& path, const QByteArray& resultJson);
    // Answers with spec/fixtures/yandex/<name>.json and the status its case name gives.
    void
    fixture(const QByteArray& method, const QString& path, const QString& name, int delayMs = 0);

    const QList<MockRequest>& requests() const { return recordedRequests; }
    const MockRequest* last(const QString& path) const;

private:
    struct PrefixRoute {
        QByteArray method;
        QString prefix;
        THandler handler;
    };

    void handle(QTcpSocket* socket, QByteArray& buffer);

    QTcpServer server;
    QHash<QByteArray, THandler> routes;
    QList<PrefixRoute> prefixRoutes;
    QList<MockRequest> recordedRequests;
};

}  // namespace Tests
