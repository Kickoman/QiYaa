#include "core/CoverCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QStandardPaths>

namespace qiyaa {

namespace {
constexpr int kMemoryItems = 30;
}

CoverCache::CoverCache(QNetworkAccessManager* nam, const QString& cacheDir, QObject* parent)
    : QObject(parent),
      m_nam(nam),
      m_dir(cacheDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/covers")
                               : cacheDir) {
    QDir().mkpath(m_dir);
}

QString CoverCache::pathFor(const QUrl& url) const {
    const QByteArray hash = QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha1).toHex();
    return m_dir + u'/' + QString::fromLatin1(hash) + QStringLiteral(".jpg");
}

QString CoverCache::localFile(const QUrl& url) const {
    if (url.isEmpty()) return {};
    const QString path = pathFor(url);
    return QFile::exists(path) ? path : QString();
}

void CoverCache::remember(const QUrl& url, const QImage& img) {
    m_images.insert(url, img);
    m_lru.removeAll(url);
    m_lru.append(url);
    while (m_lru.size() > kMemoryItems) m_images.remove(m_lru.takeFirst());
}

QImage CoverCache::get(const QUrl& url) {
    if (url.isEmpty()) return {};
    if (const auto it = m_images.constFind(url); it != m_images.cend()) {
        m_lru.removeAll(url);
        m_lru.append(url);
        return *it;
    }
    // On disk from an earlier run?
    if (const QString path = localFile(url); !path.isEmpty()) {
        const QImage img(path);
        if (!img.isNull()) {
            remember(url, img);
            return img;
        }
    }
    if (m_pending.contains(url) || !m_nam) return {};
    m_pending.insert(url);
    QNetworkRequest req(url);
    req.setTransferTimeout(20000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_nam->get(req);
    QPointer<CoverCache> self(this);
    connect(reply, &QNetworkReply::finished, this, [self, reply, url] {
        reply->deleteLater();
        if (!self) return;
        self->m_pending.remove(url);
        if (reply->error() != QNetworkReply::NoError) return;
        const QByteArray bytes = reply->readAll();
        QImage img;
        if (!img.loadFromData(bytes)) return;
        QFile f(self->pathFor(url));
        if (f.open(QIODevice::WriteOnly)) f.write(bytes);
        self->remember(url, img);
        Q_EMIT self->ready(url);
    });
    return {};
}

}  // namespace qiyaa
