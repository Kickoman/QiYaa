#include "core/cover_cache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStandardPaths>
#include <QString>

namespace Core {

namespace {
constexpr int kMemoryItems = 30;
constexpr int kTimeoutMs = 20'000;
}  // namespace

CoverCache::CoverCache(
    QNetworkAccessManager* networkAccessManager,
    const QString& cacheDirectory,
    QObject* parent
)
    : QObject(parent)
    , networkManager(networkAccessManager)
    , directory(
          cacheDirectory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                  + QStringLiteral("/covers")
                                   : cacheDirectory
      ) {
    QDir().mkpath(directory);
}

QString CoverCache::pathFor(const QUrl& url) const {
    const QByteArray hash =
        QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha1).toHex();
    return directory + u'/' + QString::fromLatin1(hash) + QStringLiteral(".jpg");
}

QString CoverCache::localFile(const QUrl& url) const {
    if (url.isEmpty()) {
        return {};
    }
    const QString path = pathFor(url);
    return QFile::exists(path) ? path : QString();
}

void CoverCache::remember(const QUrl& url, const QImage& image) {
    images.insert(url, image);
    recentlyUsed.removeAll(url);
    recentlyUsed.append(url);
    while (recentlyUsed.size() > kMemoryItems) {
        images.remove(recentlyUsed.takeFirst());
    }
}

QImage CoverCache::get(const QUrl& url) {
    if (url.isEmpty()) {
        return {};
    }
    if (const auto cached = images.constFind(url); cached != images.cend()) {
        recentlyUsed.removeAll(url);
        recentlyUsed.append(url);
        return *cached;
    }
    if (const QString path = localFile(url); !path.isEmpty()) {
        const QImage image(path);
        if (!image.isNull()) {
            remember(url, image);
            return image;
        }
    }
    if (pending.contains(url) || !networkManager) {
        return {};
    }
    pending.insert(url);
    QNetworkRequest request(url);
    request.setTransferTimeout(kTimeoutMs);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy
    );
    QNetworkReply* reply = networkManager->get(request);
    QPointer<CoverCache> self(this);
    connect(reply, &QNetworkReply::finished, reply, [self, reply, url] {
        reply->deleteLater();
        if (!self) {
            return;
        }
        self->pending.remove(url);
        if (reply->error() != QNetworkReply::NoError) {
            return;
        }
        const QByteArray bytes = reply->readAll();
        QImage image;
        if (!image.loadFromData(bytes)) {
            return;
        }
        QFile file(self->pathFor(url));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(bytes);
        }
        self->remember(url, image);
        Q_EMIT self->ready(url);
    });
    return {};
}

}  // namespace Core
