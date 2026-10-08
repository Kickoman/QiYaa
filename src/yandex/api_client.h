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

// What went wrong with a request, by kind rather than by its text. A broken or failed connection
// is Network even when a status line had arrived; then a status of 400 or more is Http; any other
// unusable reply (not JSON, no usable download variant, …) is Content.
struct RequestError {
    enum class Kind { None, Network, Http, Content };
    Kind kind = Kind::None;
    int httpStatus = 0;  // the server's status, when it answered
    QString text;

    bool isError() const { return kind != Kind::None; }
};

// The error of a finished reply: Kind::None when it succeeded. `text` is left empty.
RequestError ClassifyReply(const QNetworkReply& reply);

using TForm = QList<std::pair<QString, QString>>;

class ApiClient : public QObject {
    Q_OBJECT
public:
    template <typename T>
    using TCallback = std::function<void(const T& value, const QString& error)>;
    using TJsonCallback = std::function<void(const QJsonValue& result, const QString& error)>;
    using TUrlCallback = std::function<void(const ResolvedUrl& link, const RequestError& error)>;
    using TClassifiedJsonCallback =
        std::function<void(const QJsonValue& result, const RequestError& error)>;

    explicit ApiClient(QNetworkAccessManager* networkAccessManager, QObject* parent = nullptr);

    void setToken(const QString& token) { accessToken = token; }
    const QString& token() const { return accessToken; }
    bool hasToken() const { return !accessToken.isEmpty(); }
    QNetworkAccessManager* network() const { return networkManager; }

    void setBaseUrl(const QString& base) { baseUrl = base; }

    void getJson(const QString& path, const QUrlQuery& query, TJsonCallback callback);
    // The same GET, with the error by kind and HTTP status (a 401 tells a rejected token apart).
    void getClassifiedJson(
        const QString& path,
        const QUrlQuery& query,
        TClassifiedJsonCallback callback
    );
    void postForm(const QString& path, const TForm& form, TJsonCallback callback);
    void postJson(const QString& path, const QJsonObject& body, TJsonCallback callback);

    void accountStatus(TCallback<Account> callback);
    void tracks(const QStringList& ids, TCallback<QList<Track>> callback);
    void resolveTrackUrl(const QString& trackId, TUrlCallback callback);
    void reportPlayStarted(const Account& account, const Track& track, const QString& playId);

    int pendingPosts() const { return pendingPostCount; }

    static Track ParseTrack(const QJsonValue& value);
    static QString IdString(const QJsonValue& value);

Q_SIGNALS:
    void postsSettled();
    void requestFailed(Yandex::RequestError::Kind kind, int httpStatus);

private:
    void handleJson(QNetworkReply* reply, TJsonCallback callback);
    void handleClassifiedJson(QNetworkReply* reply, TClassifiedJsonCallback callback);
    void trackPost(QNetworkReply* reply);

    QNetworkAccessManager* networkManager;
    QString accessToken;
    QString baseUrl;
    int pendingPostCount = 0;
};

}  // namespace Yandex
