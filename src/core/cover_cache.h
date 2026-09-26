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
    explicit CoverCache(
        QNetworkAccessManager* networkAccessManager,
        const QString& cacheDirectory = {},
        QObject* parent = nullptr
    );

    QImage get(const QUrl& url);
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
