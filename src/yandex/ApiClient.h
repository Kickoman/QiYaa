// Minimal async client for the (unofficial) Yandex Music API.
// Low-level: JSON requests with auth, a few typed calls used by playback.
// Higher-level sources (playlists, waves, search...) live in Library.
#pragma once

#include <functional>

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

class QNetworkAccessManager;
class QNetworkReply;

namespace qiyaa::yandex {

struct Account {
    QString uid;
    QString login;
    QString displayName;
};

struct Track {
    QString id;       // numeric id as string
    QString albumId;  // first album, may be empty
    QString title;
    QStringList artists;
    qint64 durationMs = 0;
    bool available = true;

    QString displayTitle() const;  // "Artist1, Artist2 - Title"
    QUrl webUrl() const;           // music.yandex.ru page
};

struct ResolvedUrl {
    QUrl url;
    int bitrateKbps = 0;
};

using Form = QList<std::pair<QString, QString>>;

class ApiClient : public QObject {
    Q_OBJECT
public:
    template <typename T>
    using Callback = std::function<void(const T& value, const QString& error)>;
    // `result` is the "result" member of the response envelope.
    using JsonCallback = std::function<void(const QJsonValue& result, const QString& error)>;

    explicit ApiClient(QNetworkAccessManager* nam, QObject* parent = nullptr);

    void setToken(const QString& token) { m_token = token; }
    const QString& token() const { return m_token; }
    bool hasToken() const { return !m_token.isEmpty(); }
    QNetworkAccessManager* network() const { return m_nam; }

    // For tests: point the client at a local mock server.
    void setBaseUrl(const QString& base) { m_base = base; }

    // Generic requests.
    void getJson(const QString& path, const QUrlQuery& query, JsonCallback cb);
    void postForm(const QString& path, const Form& form, JsonCallback cb);
    void postJson(const QString& path, const QJsonObject& body, JsonCallback cb);

    // Typed calls used by the player.
    void accountStatus(Callback<Account> cb);
    void tracks(const QStringList& ids, Callback<QList<Track>> cb);
    void resolveTrackUrl(const QString& trackId, Callback<ResolvedUrl> cb);
    void reportPlayStarted(const Account& account, const Track& track, const QString& playId);

    static Track parseTrack(const QJsonValue& v);
    static QString idString(const QJsonValue& v);  // ids come as numbers or strings

private:
    void handleJson(QNetworkReply* reply, JsonCallback cb);

    QNetworkAccessManager* m_nam;
    QString m_token;
    QString m_base;
};

}  // namespace qiyaa::yandex
