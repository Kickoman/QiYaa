// Getting a Yandex OAuth token without an embedded browser.
//
// 1. Device flow: we get a short code, the user enters it at ya.ru/device in
//    any browser (even on a phone), we poll until the token is issued.
// 2. Fallback: open the login page in the system browser; after logging in,
//    Yandex redirects to music.yandex.ru/#access_token=...; the user pastes
//    that address (or the bare token) into the dialog.
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace qiyaa::yandex {

class DeviceLogin : public QObject {
    Q_OBJECT
public:
    explicit DeviceLogin(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~DeviceLogin() override;

    // For tests: point at a mock server instead of https://oauth.yandex.ru
    void setBaseUrl(const QString& base) { m_base = base; }

    void start();
    void cancel();

    // Login page for the fallback flow (implicit grant).
    static QUrl browserLoginUrl();

Q_SIGNALS:
    void codeReady(const QString& userCode, const QUrl& verificationUrl);
    void succeeded(const QString& token);
    void failed(const QString& error);

private:
    void poll();

    QNetworkAccessManager* m_nam;
    QString m_base;
    QString m_deviceCode;
    QTimer m_pollTimer;
    QPointer<QNetworkReply> m_reply;
    qint64 m_deadlineMs = 0;
};

}  // namespace qiyaa::yandex
