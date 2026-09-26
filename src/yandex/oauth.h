#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace Yandex {

class DeviceLogin : public QObject {
    Q_OBJECT
public:
    explicit DeviceLogin(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~DeviceLogin() override;

    void setBaseUrl(const QString& base) { baseUrl = base; }

    void start();
    void cancel();

    static QUrl BrowserLoginUrl();

Q_SIGNALS:
    void codeReady(const QString& userCode, const QUrl& verificationUrl);
    void succeeded(const QString& token);
    void failed(const QString& error);

private:
    void poll();

    QNetworkAccessManager* networkManager;
    QString baseUrl;
    QString deviceCode;
    QTimer pollTimer;
    QPointer<QNetworkReply> pendingReply;
    qint64 deadlineMs = 0;
};

}  // namespace Yandex
