// Album covers: downloaded once, kept in memory (small LRU) and on disk, so the
// "Now playing" window and the system media integration (MPRIS artUrl) share them.
#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QSet>
#include <QUrl>

class QNetworkAccessManager;

namespace Core {

class CoverCache : public QObject {
    Q_OBJECT
public:
    // `cacheDir` empty = QStandardPaths::CacheLocation + "/covers".
    CoverCache(QNetworkAccessManager* nam, const QString& cacheDir = {}, QObject* parent = nullptr);

    // The cover if it's already available; otherwise starts loading it and
    // returns a null image (ready() follows).
    QImage get(const QUrl& url);
    // Path of the cached file, empty if not downloaded yet.
    QString localFile(const QUrl& url) const;

Q_SIGNALS:
    void ready(const QUrl& url);

private:
    QString pathFor(const QUrl& url) const;
    void remember(const QUrl& url, const QImage& image);

    QNetworkAccessManager* networkManager;
    QString directory;
    QHash<QUrl, QImage> images;
    QList<QUrl> recentlyUsed;
    QSet<QUrl> pending;
};

}  // namespace Core
