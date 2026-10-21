#include "spotify/client/login.h"

#include "spotify/client/identity.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUrlQuery>

#include <algorithm>
#include <utility>

namespace SpotifyClient {

namespace {

constexpr int kTimeoutMs = 20'000;
constexpr int kSlowDownStepMs = 5000;
constexpr qsizetype kMaxRedirectBytes = 16 * 1024;
const QString kScope = QStringLiteral("streaming");
const QString kVerifierCharacters = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~"
);

QByteArray FormBody(const QList<std::pair<QString, QString>>& form) {
    QByteArray body;
    for (const auto& [key, value] : form) {
        if (!body.isEmpty()) {
            body += '&';
        }
        body += QUrl::toPercentEncoding(key) + '=' + QUrl::toPercentEncoding(value);
    }
    return body;
}

QNetworkRequest FormRequest(const QUrl& url) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setTransferTimeout(kTimeoutMs);
    return request;
}

QString ErrorOf(const QJsonObject& object, QNetworkReply* reply) {
    QString message = object.value(QStringLiteral("error_description")).toString();
    if (message.isEmpty()) {
        message = object.value(QStringLiteral("error")).toString();
    }
    if (message.isEmpty()) {
        message = reply->errorString();
    }
    return message;
}

QString RandomText(int length) {
    QString text;
    text.reserve(length);
    for (int i = 0; i < length; ++i) {
        text += kVerifierCharacters.at(QRandomGenerator::system()->bounded(static_cast<int>(kVerifierCharacters.size()))
        );
    }
    return text;
}

QByteArray Page(const QString& text) {
    const QByteArray body =
        "<!doctype html><meta charset=\"utf-8\"><title>QiYaa</title><p>" + text.toHtmlEscaped().toUtf8() + "</p>";
    return "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: "
        + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

}  // namespace

PairLogin::PairLogin(QNetworkAccessManager* networkAccessManager, Endpoints addresses, QObject* parent)
    : QObject(parent)
    , networkManager(networkAccessManager)
    , endpoints(std::move(addresses)) {
    pollTimer.setSingleShot(true);
    connect(&pollTimer, &QTimer::timeout, this, &PairLogin::poll);
}

PairLogin::~PairLogin() {
    cancel();
}

void PairLogin::cancel() {
    pollTimer.stop();
    if (pendingReply) {
        pendingReply->abort();
    }
    deviceCode.clear();
}

void PairLogin::start() {
    cancel();
    const QByteArray body = FormBody({{QStringLiteral("client_id"), kClientId}, {QStringLiteral("scope"), kScope}});
    const QUrl url(endpoints.accounts + QStringLiteral("/oauth2/device/authorize"));
    QNetworkReply* reply = networkManager->post(FormRequest(url), body);
    pendingReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            return;
        }
        const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
        deviceCode = object.value(QStringLiteral("device_code")).toString();
        const QString userCode = object.value(QStringLiteral("user_code")).toString();
        if (deviceCode.isEmpty() || userCode.isEmpty()) {
            deviceCode.clear();
            Q_EMIT failed(ErrorOf(object, reply));
            return;
        }
        QUrl page(object.value(QStringLiteral("verification_uri_complete")).toString());
        if (!page.isValid() || page.isEmpty()) {
            page = QUrl(
                object.value(QStringLiteral("verification_uri")).toString(QStringLiteral("https://spotify.com/pair"))
            );
        }
        const int intervalSeconds = std::max(1, object.value(QStringLiteral("interval")).toInt(5));
        const int lifetimeSeconds = object.value(QStringLiteral("expires_in")).toInt(600);
        deadlineMs = QDateTime::currentMSecsSinceEpoch() + lifetimeSeconds * 1000LL;
        pollTimer.setInterval(intervalSeconds * 1000);
        Q_EMIT codeReady(userCode, page);
        pollTimer.start();
    });
}

void PairLogin::poll() {
    if (deviceCode.isEmpty()) {
        return;
    }
    if (QDateTime::currentMSecsSinceEpoch() > deadlineMs) {
        deviceCode.clear();
        Q_EMIT failed(tr("the code has expired, start again"));
        return;
    }
    const QByteArray body = FormBody(
        {{QStringLiteral("grant_type"), QStringLiteral("urn:ietf:params:oauth:grant-type:device_code")},
         {QStringLiteral("device_code"), deviceCode},
         {QStringLiteral("client_id"), kClientId}}
    );
    QNetworkReply* reply =
        networkManager->post(FormRequest(QUrl(endpoints.accounts + QStringLiteral("/api/token"))), body);
    pendingReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            return;
        }
        const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
        const QString token = object.value(QStringLiteral("access_token")).toString();
        if (!token.isEmpty()) {
            deviceCode.clear();
            Q_EMIT succeeded(token);
            return;
        }
        const QString error = object.value(QStringLiteral("error")).toString();
        if (error == QLatin1String("authorization_pending") || error == QLatin1String("slow_down")) {
            if (error == QLatin1String("slow_down")) {
                pollTimer.setInterval(pollTimer.interval() + kSlowDownStepMs);
            }
            pollTimer.start();
            return;
        }
        deviceCode.clear();
        Q_EMIT failed(ErrorOf(object, reply));
    });
}

BrowserLogin::BrowserLogin(QNetworkAccessManager* networkAccessManager, Endpoints addresses, QObject* parent)
    : QObject(parent)
    , networkManager(networkAccessManager)
    , endpoints(std::move(addresses)) {
    connect(&server, &QTcpServer::newConnection, this, &BrowserLogin::accept);
}

BrowserLogin::~BrowserLogin() {
    cancel();
}

void BrowserLogin::cancel() {
    server.close();
    if (pendingReply) {
        pendingReply->abort();
    }
    verifier.clear();
    state.clear();
}

void BrowserLogin::start() {
    cancel();
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        Q_EMIT failed(server.errorString());
        return;
    }
    redirectUri = QStringLiteral("http://127.0.0.1:%1/login").arg(server.serverPort());
    verifier = RandomText(64);
    state = RandomText(24);
    const QByteArray challenge =
        QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256)
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QUrl url(endpoints.accounts + QStringLiteral("/authorize"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), kClientId);
    query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
    query.addQueryItem(QStringLiteral("scope"), kScope);
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("code_challenge"), QString::fromLatin1(challenge));
    query.addQueryItem(QStringLiteral("state"), state);
    url.setQuery(query);
    Q_EMIT openUrl(url);
}

void BrowserLogin::accept() {
    while (QTcpSocket* socket = server.nextPendingConnection()) {
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readRedirect(socket); });
    }
}

void BrowserLogin::readRedirect(QTcpSocket* socket) {
    QByteArray head = socket->property("head").toByteArray() + socket->readAll();
    if (!head.contains("\r\n\r\n") && head.size() < kMaxRedirectBytes) {
        socket->setProperty("head", head);
        return;
    }
    const QList<QByteArray> requestLine = head.left(head.indexOf("\r\n")).split(' ');
    const QUrl target(QString::fromLatin1(requestLine.value(1)));
    if (requestLine.value(0) != "GET" || target.path() != QLatin1String("/login") || verifier.isEmpty()) {
        socket->write("HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
        socket->disconnectFromHost();
        return;
    }
    const QUrlQuery query(target);
    const QString code = query.queryItemValue(QStringLiteral("code"));
    const QString error = query.queryItemValue(QStringLiteral("error"));
    const bool matches = query.queryItemValue(QStringLiteral("state")) == state;
    if (code.isEmpty() || !matches) {
        socket->write(Page(tr("QiYaa did not get access to Spotify. You can close this page.")));
        socket->disconnectFromHost();
        cancel();
        Q_EMIT failed(error.isEmpty() ? tr("the browser's answer does not match the request") : error);
        return;
    }
    socket->write(Page(tr("QiYaa is logging in to Spotify. You can close this page.")));
    socket->disconnectFromHost();
    server.close();
    exchange(code);
}

void BrowserLogin::exchange(const QString& code) {
    const QByteArray body = FormBody(
        {{QStringLiteral("grant_type"), QStringLiteral("authorization_code")},
         {QStringLiteral("code"), code},
         {QStringLiteral("redirect_uri"), redirectUri},
         {QStringLiteral("client_id"), kClientId},
         {QStringLiteral("code_verifier"), verifier}}
    );
    QNetworkReply* reply =
        networkManager->post(FormRequest(QUrl(endpoints.accounts + QStringLiteral("/api/token"))), body);
    pendingReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            return;
        }
        const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
        const QString token = object.value(QStringLiteral("access_token")).toString();
        verifier.clear();
        state.clear();
        if (token.isEmpty()) {
            Q_EMIT failed(ErrorOf(object, reply));
            return;
        }
        Q_EMIT succeeded(token);
    });
}

}  // namespace SpotifyClient
