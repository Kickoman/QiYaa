#include "yandex/oauth.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QUrlQuery>

namespace Yandex {

namespace {
// Yandex Music's public OAuth client — the same one Yaamp's login page and
// other unofficial clients (yandex-music-api, etc.) use.
constexpr char kClientId[] = "23cabbbdc6cd418abb4b39c32c41195d";
constexpr char kClientSecret[] = "53bc75238f0c4d08a118e51fe9203300";

QByteArray FormBody(const QList<std::pair<QString, QString>>& form) {
    QByteArray body;
    for (const auto& [k, v] : form) {
        if (!body.isEmpty()) {
            body += '&';
        }
        body += QUrl::toPercentEncoding(k) + '=' + QUrl::toPercentEncoding(v);
    }
    return body;
}

QNetworkRequest FormRequest(const QUrl& url) {
    QNetworkRequest req(url);
    req.setHeader(
        QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded")
    );
    req.setTransferTimeout(20000);
    return req;
}
}  // namespace

DeviceLogin::DeviceLogin(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent)
    , networkManager(nam)
    , baseUrl(QStringLiteral("https://oauth.yandex.ru")) {
    pollTimer.setSingleShot(true);
    connect(&pollTimer, &QTimer::timeout, this, &DeviceLogin::poll);
}

DeviceLogin::~DeviceLogin() {
    cancel();
}

QUrl DeviceLogin::BrowserLoginUrl() {
    QUrl url(QStringLiteral("https://oauth.yandex.ru/authorize"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("token"));
    q.addQueryItem(QStringLiteral("client_id"), QString::fromLatin1(kClientId));
    url.setQuery(q);
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
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        deviceCode = o.value(QStringLiteral("device_code")).toString();
        const QString userCode = o.value(QStringLiteral("user_code")).toString();
        if (deviceCode.isEmpty() || userCode.isEmpty()) {
            QString err = o.value(QStringLiteral("error_description")).toString();
            if (err.isEmpty()) {
                err = o.value(QStringLiteral("error")).toString();
            }
            if (err.isEmpty()) {
                err = reply->errorString();
            }
            Q_EMIT failed(err);
            return;
        }
        QUrl verify(o.value(QStringLiteral("verification_url")).toString());
        if (!verify.isValid() || verify.isEmpty()) {
            verify = QUrl(QStringLiteral("https://ya.ru/device"));
        }
        const int interval = std::max(1, o.value(QStringLiteral("interval")).toInt(5));
        const int expires = o.value(QStringLiteral("expires_in")).toInt(300);
        deadlineMs = QDateTime::currentMSecsSinceEpoch() + expires * 1000LL;
        pollTimer.setInterval(interval * 1000);
        Q_EMIT codeReady(userCode, verify);
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
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const QString token = o.value(QStringLiteral("access_token")).toString();
        if (!token.isEmpty()) {
            deviceCode.clear();
            Q_EMIT succeeded(token);
            return;
        }
        const QString error = o.value(QStringLiteral("error")).toString();
        if (error == QLatin1String("authorization_pending")
            || error == QLatin1String("slow_down")) {
            if (error == QLatin1String("slow_down")) {
                pollTimer.setInterval(pollTimer.interval() + 2000);
            }
            pollTimer.start();
            return;
        }
        QString desc = o.value(QStringLiteral("error_description")).toString();
        if (desc.isEmpty()) {
            desc = error.isEmpty() ? reply->errorString() : error;
        }
        deviceCode.clear();
        Q_EMIT failed(desc);
    });
}

}  // namespace Yandex
