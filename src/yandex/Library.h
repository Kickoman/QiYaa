// High-level access to the user's Yandex Music library: likes, playlists,
// artists, albums, stations/waves, search, like/dislike.
// Endpoints mirror what Yaamp used.
#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "yandex/ApiClient.h"

namespace qiyaa::yandex {

struct NamedRef {
    QString id;
    QString name;
};

struct PlaylistRef {
    QString ownerUid;
    QString kind;
    QString title;
    int trackCount = 0;
};

struct Station {
    QString id;    // "type:tag", e.g. "genre:rock", "user:onyourwave"
    QString type;  // "genre", "mood", ...
    QString name;
};

struct WaveBatch {
    QString sessionId;
    QString batchId;
    QList<Track> tracks;
};

struct SearchResult {
    QString bestType;  // "artist", "album", "track", "playlist" or empty
    QString bestId;
    QString bestName;
    QList<Track> tracks;
};

class Library : public QObject {
    Q_OBJECT
public:
    template <typename T>
    using Callback = ApiClient::Callback<T>;

    explicit Library(ApiClient* api, QObject* parent = nullptr);

    ApiClient* api() const { return m_api; }

    // Loads the account for the API client's current token.
    void connectAccount(Callback<Account> cb);
    void logout();
    bool isLoggedIn() const { return !m_account.uid.isEmpty(); }
    const Account& account() const { return m_account; }

    void likedTracks(Callback<QList<Track>> cb);
    void tracksByIds(const QStringList& ids, Callback<QList<Track>> cb);  // chunked, order kept
    void userPlaylists(Callback<QList<PlaylistRef>> cb);
    void playlistTracks(const PlaylistRef& playlist, Callback<QList<Track>> cb);
    void likedArtists(Callback<QList<NamedRef>> cb);
    void artistTopTracks(const QString& artistId, Callback<QList<Track>> cb);
    void likedAlbums(Callback<QList<NamedRef>> cb);
    void albumTracks(const QString& albumId, Callback<QList<Track>> cb);
    void stations(Callback<QList<Station>> cb);
    void startWave(const QStringList& seeds, Callback<WaveBatch> cb);
    void moreWave(const QString& sessionId, const QStringList& queue, Callback<WaveBatch> cb);
    void search(const QString& text, Callback<SearchResult> cb);

    bool isLiked(const QString& trackId) const { return m_likedIds.contains(trackId); }
    void setLiked(const QString& trackId, bool liked, Callback<bool> cb);
    void dislike(const QString& trackId, Callback<bool> cb);

    static QList<Track> parseTrackArray(const QJsonArray& arr);
    static WaveBatch parseWaveBatch(const QJsonValue& result);

Q_SIGNALS:
    void accountChanged();
    void likesChanged();

private:
    void tracksChunk(QStringList remaining, QList<Track> acc, Callback<QList<Track>> cb);
    QString userPath(const QString& rest) const;

    ApiClient* m_api;
    Account m_account;
    QSet<QString> m_likedIds;
};

}  // namespace qiyaa::yandex
