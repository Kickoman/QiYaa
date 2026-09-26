#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include <functional>
#include <utility>

class QNetworkAccessManager;
class QNetworkReply;

namespace Yandex {

struct Account {
    QString uid;
    QString login;
    QString displayName;
};

struct Track {
    QString id;
    QString albumId;
    QString title;
    QStringList artists;
    qint64 durationMs = 0;
    bool available = true;
    QString albumTitle;
    int year = 0;
    QString genre;
    QString coverUri;

    QString displayTitle() const;
    QUrl webUrl() const;
    QUrl coverUrl(int size = 400) const;
};

struct ResolvedUrl {
    QUrl url;
    int bitrateKbps = 0;
};

using TForm = QList<std::pair<QString, QString>>;

class ApiClient : public QObject {
    Q_OBJECT
public:
    template <typename T>
    using TCallback = std::function<void(const T& value, const QString& error)>;
    using TJsonCallback = std::function<void(const QJsonValue& result, const QString& error)>;

    explicit ApiClient(QNetworkAccessManager* nam, QObject* parent = nullptr);

    void setToken(const QString& token) { accessToken = token; }
    const QString& token() const { return accessToken; }
    bool hasToken() const { return !accessToken.isEmpty(); }
    QNetworkAccessManager* network() const { return networkManager; }

    void setBaseUrl(const QString& base) { baseUrl = base; }

    void getJson(const QString& path, const QUrlQuery& query, TJsonCallback callback);
    void postForm(const QString& path, const TForm& form, TJsonCallback callback);
    void postJson(const QString& path, const QJsonObject& body, TJsonCallback callback);

    void accountStatus(TCallback<Account> callback);
    void tracks(const QStringList& ids, TCallback<QList<Track>> callback);
    void resolveTrackUrl(const QString& trackId, TCallback<ResolvedUrl> callback);
    void reportPlayStarted(const Account& account, const Track& track, const QString& playId);

    int pendingPosts() const { return pendingPostCount; }

    static Track ParseTrack(const QJsonValue& value);
    static QString IdString(const QJsonValue& value);

Q_SIGNALS:
    void postsSettled();

private:
    void handleJson(QNetworkReply* reply, TJsonCallback callback);
    void trackPost(QNetworkReply* reply);

    QNetworkAccessManager* networkManager;
    QString accessToken;
    QString baseUrl;
    int pendingPostCount = 0;
};

}  // namespace Yandex
