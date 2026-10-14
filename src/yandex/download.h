#pragma once

#include "yandex/api_client.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>

class QNetworkReply;

namespace Yandex {

class Download : public QObject {
    Q_OBJECT
public:
    explicit Download(QNetworkReply* reply);
    ~Download() override;

Q_SIGNALS:
    void received(const QByteArray& bytes);
    void finished(const Yandex::RequestError& error);

private:
    QPointer<QNetworkReply> reply;
};

}  // namespace Yandex
