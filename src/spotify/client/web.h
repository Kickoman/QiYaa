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

    void spclient(
        const QByteArray& method,
        const QString& pathAndQuery,
        const QByteArray& body,
        const QByteArray& contentType,
        TReply done
    );
    void forgetTokens();
    QByteArray userAgent() const { return agent; }

private:
    using TReady = std::function<void(const WebReply* failure)>;

    void withTokens(TReady ready);
    void fetchClientToken(const QByteArray& body, int round);
    void fetchBearer(const QByteArray& requestBody, int round);
    void finishTokens(const WebReply* failure);
    void send(
        const QByteArray& method,
        const QString& pathAndQuery,
        const QByteArray& body,
        const QByteArray& contentType,
        TReply done,
        bool retried
    );
    QNetworkReply* post(const QNetworkRequest& request, const QByteArray& body);
    static WebReply ReplyOf(QNetworkReply* reply);

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
