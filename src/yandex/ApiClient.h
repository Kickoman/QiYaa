// Minimal async client for the (unofficial) Yandex Music API.
// Prototype scope: account, liked tracks, track metadata, direct mp3 link, play-audio.
#pragma once

#include <functional>

#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

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
};

struct ResolvedUrl {
    QUrl url;
    int bitrateKbps = 0;
};

class ApiClient : public QObject {
    Q_OBJECT
public:
    template <typename T>
    using Callback = std::function<void(const T& value, const QString& error)>;

    explicit ApiClient(QNetworkAccessManager* nam, QObject* parent = nullptr);

    void setToken(const QString& token) { m_token = token; }
    bool hasToken() const { return !m_token.isEmpty(); }
    QNetworkAccessManager* network() const { return m_nam; }

    void accountStatus(Callback<Account> cb);
    void likedTrackIds(const QString& uid, Callback<QStringList> cb);
    void tracks(const QStringList& ids, Callback<QList<Track>> cb);
    void resolveTrackUrl(const QString& trackId, Callback<ResolvedUrl> cb);
    void reportPlayStarted(const Account& account, const Track& track, const QString& playId);

    static Track parseTrack(const QJsonValue& v);

private:
    using JsonCallback = std::function<void(const QJsonValue& result, const QString& error)>;

    QNetworkReply* get(const QString& path);
    QNetworkReply* postForm(const QString& path, const QList<std::pair<QString, QString>>& form);
    void handleJson(QNetworkReply* reply, JsonCallback cb);

    QNetworkAccessManager* m_nam;
    QString m_token;
};

}  // namespace qiyaa::yandex
