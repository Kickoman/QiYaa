#include "yandex/download.h"

#include <QNetworkReply>
#include <QNetworkRequest>

namespace Yandex {

namespace {
bool IsHttpError(const QNetworkReply& reply) {
    return reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 400;
}
}  // namespace

Download::Download(QNetworkReply* networkReply)
    : reply(networkReply) {
    connect(networkReply, &QNetworkReply::readyRead, this, [this, networkReply] {
        const QByteArray bytes = networkReply->readAll();
        if (!IsHttpError(*networkReply)) {
            Q_EMIT received(bytes);
        }
    });
    connect(networkReply, &QNetworkReply::finished, this, [this, networkReply] {
        networkReply->deleteLater();
        RequestError error = ClassifyReply(*networkReply);
        if (error.isError()) {
            error.text = networkReply->errorString();
        } else if (const QByteArray rest = networkReply->readAll(); !rest.isEmpty()) {
            const QPointer<Download> self(this);
            Q_EMIT received(rest);
            if (!self) {
                return;
            }
        }
        Q_EMIT finished(error);
    });
}

Download::~Download() {
    if (QNetworkReply* networkReply = reply) {
        disconnect(networkReply, nullptr, this, nullptr);
        networkReply->abort();
        networkReply->deleteLater();
    }
}

}  // namespace Yandex
