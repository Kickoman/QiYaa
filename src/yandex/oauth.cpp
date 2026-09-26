#include "yandex/oauth.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QUrlQuery>

#include <algorithm>
#include <utility>

namespace Yandex {

namespace {
// Yandex Music's public OAuth client, the one Yaamp and yandex-music-api use.
constexpr char kClientId[] = "23cabbbdc6cd418abb4b39c32c41195d";
constexpr char kClientSecret[] = "53bc75238f0c4d08a118e51fe9203300";
constexpr int kTimeoutMs = 20'000;
constexpr int kSlowDownStepMs = 2000;

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
    request.setHeader(
        QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded")
    );
    request.setTransferTimeout(kTimeoutMs);
    return request;
}
}  // namespace

DeviceLogin::DeviceLogin(QNetworkAccessManager* networkAccessManager, QObject* parent)
    : QObject(parent)
    , networkManager(networkAccessManager)
    , baseUrl(QStringLiteral("https://oauth.yandex.ru")) {
    pollTimer.setSingleShot(true);
    connect(&pollTimer, &QTimer::timeout, this, &DeviceLogin::poll);
}

DeviceLogin::~DeviceLogin() {
    cancel();
}

QUrl DeviceLogin::BrowserLoginUrl() {
    QUrl url(QStringLiteral("https://oauth.yandex.ru/authorize"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("token"));
    query.addQueryItem(QStringLiteral("client_id"), QString::fromLatin1(kClientId));
    url.setQuery(query);
    return url;
}

void DeviceLogin::cancel() {
    pollTimer.stop();
    if (pendingReply) {
        pendingReply->abort();
    }
    deviceCode.clear();
}

void DeviceLogin::start() {
    cancel();
    const QByteArray body = FormBody(
        {{QStringLiteral("client_id"), QString::fromLatin1(kClientId)},
         {QStringLiteral("device_name"),
          QStringLiteral("QiYaa (%1)").arg(QSysInfo::machineHostName())}}
    );
    QNetworkReply* reply =
        networkManager->post(FormRequest(QUrl(baseUrl + QStringLiteral("/device/code"))), body);
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
            QString message = object.value(QStringLiteral("error_description")).toString();
            if (message.isEmpty()) {
                message = object.value(QStringLiteral("error")).toString();
            }
            if (message.isEmpty()) {
                message = reply->errorString();
            }
            Q_EMIT failed(message);
            return;
        }
        QUrl verificationUrl(object.value(QStringLiteral("verification_url")).toString());
        if (!verificationUrl.isValid() || verificationUrl.isEmpty()) {
            verificationUrl = QUrl(QStringLiteral("https://ya.ru/device"));
        }
        const int intervalSeconds = std::max(1, object.value(QStringLiteral("interval")).toInt(5));
        const int lifetimeSeconds = object.value(QStringLiteral("expires_in")).toInt(300);
        deadlineMs = QDateTime::currentMSecsSinceEpoch() + lifetimeSeconds * 1000LL;
        pollTimer.setInterval(intervalSeconds * 1000);
        Q_EMIT codeReady(userCode, verificationUrl);
        pollTimer.start();
    });
}

void DeviceLogin::poll() {
    if (deviceCode.isEmpty()) {
        return;
    }
    if (QDateTime::currentMSecsSinceEpoch() > deadlineMs) {
        Q_EMIT failed(QStringLiteral("код устарел, начните заново"));
        return;
    }
    const QByteArray body = FormBody(
        {{QStringLiteral("grant_type"), QStringLiteral("device_code")},
         {QStringLiteral("code"), deviceCode},
         {QStringLiteral("client_id"), QString::fromLatin1(kClientId)},
         {QStringLiteral("client_secret"), QString::fromLatin1(kClientSecret)}}
    );
    QNetworkReply* reply =
        networkManager->post(FormRequest(QUrl(baseUrl + QStringLiteral("/token"))), body);
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
        if (error == QLatin1String("authorization_pending")
            || error == QLatin1String("slow_down")) {
            if (error == QLatin1String("slow_down")) {
                pollTimer.setInterval(pollTimer.interval() + kSlowDownStepMs);
            }
            pollTimer.start();
            return;
        }
        QString description = object.value(QStringLiteral("error_description")).toString();
        if (description.isEmpty()) {
            description = error.isEmpty() ? reply->errorString() : error;
        }
        deviceCode.clear();
        Q_EMIT failed(description);
    });
}

}  // namespace Yandex
