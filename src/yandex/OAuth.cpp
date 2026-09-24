#include "yandex/OAuth.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QUrlQuery>

namespace qiyaa::yandex {

namespace {
// Yandex Music's public OAuth client — the same one Yaamp's login page and
// other unofficial clients (yandex-music-api, etc.) use.
constexpr char kClientId[] = "23cabbbdc6cd418abb4b39c32c41195d";
constexpr char kClientSecret[] = "53bc75238f0c4d08a118e51fe9203300";

QByteArray formBody(const QList<std::pair<QString, QString>>& form) {
    QByteArray body;
    for (const auto& [k, v] : form) {
        if (!body.isEmpty()) body += '&';
        body += QUrl::toPercentEncoding(k) + '=' + QUrl::toPercentEncoding(v);
    }
    return body;
}

QNetworkRequest formRequest(const QUrl& url) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    req.setTransferTimeout(20000);
    return req;
}
}  // namespace

DeviceLogin::DeviceLogin(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam), m_base(QStringLiteral("https://oauth.yandex.ru")) {
    m_pollTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, &DeviceLogin::poll);
}

DeviceLogin::~DeviceLogin() {
    cancel();
}

QUrl DeviceLogin::browserLoginUrl() {
    QUrl url(QStringLiteral("https://oauth.yandex.ru/authorize"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("token"));
    q.addQueryItem(QStringLiteral("client_id"), QString::fromLatin1(kClientId));
    url.setQuery(q);
    return url;
}

void DeviceLogin::cancel() {
    m_pollTimer.stop();
    if (m_reply) m_reply->abort();
    m_deviceCode.clear();
}

void DeviceLogin::start() {
    cancel();
    const QByteArray body = formBody({{QStringLiteral("client_id"), QString::fromLatin1(kClientId)},
                                      {QStringLiteral("device_name"), QStringLiteral("QiYaa (%1)").arg(QSysInfo::machineHostName())}});
    QNetworkReply* reply = m_nam->post(formRequest(QUrl(m_base + QStringLiteral("/device/code"))), body);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::OperationCanceledError) return;
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        m_deviceCode = o.value(QStringLiteral("device_code")).toString();
        const QString userCode = o.value(QStringLiteral("user_code")).toString();
        if (m_deviceCode.isEmpty() || userCode.isEmpty()) {
            QString err = o.value(QStringLiteral("error_description")).toString();
            if (err.isEmpty()) err = o.value(QStringLiteral("error")).toString();
            if (err.isEmpty()) err = reply->errorString();
            Q_EMIT failed(err);
            return;
        }
        QUrl verify(o.value(QStringLiteral("verification_url")).toString());
        if (!verify.isValid() || verify.isEmpty()) verify = QUrl(QStringLiteral("https://ya.ru/device"));
        const int interval = std::max(1, o.value(QStringLiteral("interval")).toInt(5));
        const int expires = o.value(QStringLiteral("expires_in")).toInt(300);
        m_deadlineMs = QDateTime::currentMSecsSinceEpoch() + expires * 1000LL;
        m_pollTimer.setInterval(interval * 1000);
        Q_EMIT codeReady(userCode, verify);
        m_pollTimer.start();
    });
}

void DeviceLogin::poll() {
    if (m_deviceCode.isEmpty()) return;
    if (QDateTime::currentMSecsSinceEpoch() > m_deadlineMs) {
        Q_EMIT failed(QStringLiteral("код устарел, начните заново"));
        return;
    }
    const QByteArray body = formBody({{QStringLiteral("grant_type"), QStringLiteral("device_code")},
                                      {QStringLiteral("code"), m_deviceCode},
                                      {QStringLiteral("client_id"), QString::fromLatin1(kClientId)},
                                      {QStringLiteral("client_secret"), QString::fromLatin1(kClientSecret)}});
    QNetworkReply* reply = m_nam->post(formRequest(QUrl(m_base + QStringLiteral("/token"))), body);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::OperationCanceledError) return;
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const QString token = o.value(QStringLiteral("access_token")).toString();
        if (!token.isEmpty()) {
            m_deviceCode.clear();
            Q_EMIT succeeded(token);
            return;
        }
        const QString error = o.value(QStringLiteral("error")).toString();
        if (error == QLatin1String("authorization_pending") || error == QLatin1String("slow_down")) {
            if (error == QLatin1String("slow_down")) m_pollTimer.setInterval(m_pollTimer.interval() + 2000);
            m_pollTimer.start();
            return;
        }
        QString desc = o.value(QStringLiteral("error_description")).toString();
        if (desc.isEmpty()) desc = error.isEmpty() ? reply->errorString() : error;
        m_deviceCode.clear();
        Q_EMIT failed(desc);
    });
}

}  // namespace qiyaa::yandex
