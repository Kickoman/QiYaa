// Minimal async client for the (unofficial) Yandex Music API.
// Low-level: JSON requests with auth, a few typed calls used by playback.
// Higher-level sources (playlists, waves, search...) live in Library.
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

class QNetworkAccessManager;
class QNetworkReply;

namespace Yandex {

struct Account {
    QString uid;
    QString login;
    QString displayName;
};

struct Track {
    QString id;  // numeric id as string
    QString albumId;  // first album, may be empty
    QString title;
    QStringList artists;
    qint64 durationMs = 0;
    bool available = true;
    QString albumTitle;
    int year = 0;
    QString genre;
    QString coverUri;  // "avatars.yandex.net/get-music-content/.../%%" (%% = size)

    QString displayTitle() const;  // "Artist1, Artist2 - Title"
    QUrl webUrl() const;  // music.yandex.ru page
    QUrl coverUrl(int size = 400) const;  // empty if the track has no cover
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
    // `result` is the "result" member of the response envelope.
    using TJsonCallback = std::function<void(const QJsonValue& result, const QString& error)>;

    explicit ApiClient(QNetworkAccessManager* nam, QObject* parent = nullptr);

    void setToken(const QString& token) { accessToken = token; }
    const QString& token() const { return accessToken; }
    bool hasToken() const { return !accessToken.isEmpty(); }
    QNetworkAccessManager* network() const { return networkManager; }

    // For tests: point the client at a local mock server.
    void setBaseUrl(const QString& base) { baseUrl = base; }

    // Generic requests.
    void getJson(const QString& path, const QUrlQuery& query, TJsonCallback callback);
    void postForm(const QString& path, const TForm& form, TJsonCallback callback);
    void postJson(const QString& path, const QJsonObject& body, TJsonCallback callback);

    // Typed calls used by the player.
    void accountStatus(TCallback<Account> callback);
    void tracks(const QStringList& ids, TCallback<QList<Track>> callback);
    void resolveTrackUrl(const QString& trackId, TCallback<ResolvedUrl> callback);
    void reportPlayStarted(const Account& account, const Track& track, const QString& playId);

    // POSTs (play reports, wave feedback) still on their way; see postsSettled().
    int pendingPosts() const { return pendingPostCount; }

    static Track ParseTrack(const QJsonValue& value);
    static QString IdString(const QJsonValue& value);  // ids come as numbers or strings

Q_SIGNALS:
    // pendingPosts() dropped to 0 (after the callbacks, which may send more).
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
