#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>
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
    // >= 0: the Content-Length is the whole body's, but only this many bytes are sent before the
    // connection closes: a download cut short by the network.
    qsizetype truncateAfter = -1;
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

    // Lets the Player stream these track ids from this server: their download-info, the
    // download-info link, the signed /get-mp3/ link (all serving `mp3`) and /play-audio.
    void audioTracks(const QStringList& trackIds, const QByteArray& mp3);

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

// Sends the https:// links that point at 127.0.0.1 over http, so signed track links reach the
// MockHttpServer.
class LocalNetworkAccessManager : public QNetworkAccessManager {
protected:
    QNetworkReply*
    createRequest(Operation op, const QNetworkRequest& request, QIODevice* outgoingData) override;
};

}  // namespace Tests
