#pragma once

#include "spotify/client/endpoints.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpSocket;

namespace SpotifyClient {

class PairLogin : public QObject {
    Q_OBJECT
public:
    PairLogin(QNetworkAccessManager* networkAccessManager, Endpoints endpoints, QObject* parent = nullptr);
    ~PairLogin() override;

    void start();
    void cancel();

Q_SIGNALS:
    void codeReady(const QString& userCode, const QUrl& verificationUrl);
    void succeeded(const QString& accessToken);
    void failed(const QString& error);

private:
    void poll();

    QNetworkAccessManager* networkManager;
    Endpoints endpoints;
    QString deviceCode;
    QTimer pollTimer;
    QPointer<QNetworkReply> pendingReply;
    qint64 deadlineMs = 0;
};

class BrowserLogin : public QObject {
    Q_OBJECT
public:
    BrowserLogin(QNetworkAccessManager* networkAccessManager, Endpoints endpoints, QObject* parent = nullptr);
    ~BrowserLogin() override;

    void start();
    void cancel();

Q_SIGNALS:
    void openUrl(const QUrl& url);
    void succeeded(const QString& accessToken);
    void failed(const QString& error);

private:
    void accept();
    void readRedirect(QTcpSocket* socket);
    void exchange(const QString& code);

    QNetworkAccessManager* networkManager;
    Endpoints endpoints;
    QTcpServer server;
    QString redirectUri;
    QString verifier;
    QString state;
    QPointer<QNetworkReply> pendingReply;
};

}  // namespace SpotifyClient
