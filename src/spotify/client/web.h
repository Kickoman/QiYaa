#pragma once

#include "spotify/client/endpoints.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

namespace SpotifyClient {

class Session;

struct WebReply {
    int status = 0;
    QByteArray body;
    QString error;
    bool network = false;

    bool ok() const { return status >= 200 && status < 300; }
};

enum class WebProblem { Http, Content, QueryOutdated };

struct WebRequest {
    QByteArray method = QByteArrayLiteral("GET");
    QString path;
    QByteArray body{};
    QByteArray contentType{};
    QByteArray accept = QByteArrayLiteral("application/x-protobuf");
    bool accountQuery = true;
};

class Web : public QObject {
    Q_OBJECT
public:
    using TReply = std::function<void(const WebReply& reply)>;

    static constexpr int kTimeoutMs = 20'000;
    static constexpr int kLongestRetryAfterMs = 5000;

    Web(QNetworkAccessManager* networkAccessManager,
        Endpoints endpoints,
        Session* session,
        QString deviceId,
        QString appVersion,
        QObject* parent = nullptr);

    void spclient(const WebRequest& request, TReply done);
    void pathfinder(const QByteArray& query, TReply done);
    void forgetTokens();
    void report(WebProblem problem, int httpStatus = 0);
    QByteArray userAgent() const { return agent; }

Q_SIGNALS:
    void problem(SpotifyClient::WebProblem problem, int httpStatus);

private:
    using TReady = std::function<void(const WebReply* failure)>;

    void withTokens(TReady ready);
    void fetchClientToken(const QByteArray& body, int round);
    void fetchBearer(const QByteArray& requestBody, int round);
    void finishTokens(const WebReply* failure);
    void send(const WebRequest& request, bool onPathfinder, TReply done, bool retried);
    QNetworkReply* post(const QNetworkRequest& request, const QByteArray& body);
    static WebReply ReplyOf(QNetworkReply* reply);
    void reportFailed(const WebReply& reply);

    QNetworkAccessManager* networkManager;
    Endpoints endpoints;
    Session* session;
    QString device;
    QByteArray agent;
    QString clientToken;
    qint64 clientTokenUntilMs = 0;
    QString bearer;
    qint64 bearerUntilMs = 0;
    QList<TReady> waiting;
    bool fetching = false;
};

}  // namespace SpotifyClient
