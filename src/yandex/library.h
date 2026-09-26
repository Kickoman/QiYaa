#pragma once

#include "yandex/api_client.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace Yandex {

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
    QString id;
    QString type;
    QString name;
};

struct WaveBatch {
    QString sessionId;
    QString batchId;
    QList<Track> tracks;
};

struct Wave {
    QString name;
    QString description;
    QStringList seeds;
};

enum class WaveEvent { RadioStarted, TrackStarted, TrackFinished, Skip };

struct SearchResult {
    enum class Kind { None, Artist, Album, Track, Playlist, Other };

    Kind bestKind = Kind::None;
    QString bestId;
    QString bestName;
    QList<Track> tracks;
};

class Library : public QObject {
    Q_OBJECT
public:
    template <typename T>
    using TCallback = ApiClient::TCallback<T>;

    explicit Library(ApiClient* api, QObject* parent = nullptr);

    ApiClient* api() const { return apiClient; }

    void connectAccount(TCallback<Account> callback);
    void logout();
    bool isLoggedIn() const { return !accountData.uid.isEmpty(); }
    const Account& account() const { return accountData; }

    void likedTracks(TCallback<QList<Track>> callback);
    void tracksByIds(const QStringList& ids, TCallback<QList<Track>> callback);
    void userPlaylists(TCallback<QList<PlaylistRef>> callback);
    void playlistTracks(const PlaylistRef& playlist, TCallback<QList<Track>> callback);
    void likedArtists(TCallback<QList<NamedRef>> callback);
    void artistTopTracks(const QString& artistId, TCallback<QList<Track>> callback);
    void likedAlbums(TCallback<QList<NamedRef>> callback);
    void albumTracks(const QString& albumId, TCallback<QList<Track>> callback);
    void stations(TCallback<QList<Station>> callback);
    void startWave(const QStringList& seeds, TCallback<WaveBatch> callback);
    void
    moreWave(const QString& sessionId, const QStringList& queue, TCallback<WaveBatch> callback);
    void search(const QString& text, TCallback<SearchResult> callback);

    void personalPlaylists(TCallback<QList<PlaylistRef>> callback);
    void playlistRecommendations(const PlaylistRef& playlist, TCallback<QList<Track>> callback);
    void wheelWaves(const QStringList& seeds, TCallback<QList<Wave>> callback);

    void waveFeedback(
        const QString& sessionId,
        const QString& stationId,
        const QString& batchId,
        WaveEvent event,
        const Track* track = nullptr,
        double playedSeconds = 0
    );
    static QString WaveEventName(WaveEvent event);

    bool isLiked(const QString& trackId) const { return likedIds.contains(trackId); }
    void setLiked(const QString& trackId, bool liked, TCallback<bool> callback);
    void dislike(const QString& trackId, TCallback<bool> callback);

    static QList<Track> ParseTrackArray(const QJsonArray& arr);
    static WaveBatch ParseWaveBatch(const QJsonValue& result);

Q_SIGNALS:
    void accountChanged();
    void likesChanged();

private:
    void tracksChunk(QStringList remaining, QList<Track> acc, TCallback<QList<Track>> callback);
    void tracksFromItems(const QJsonArray& items, TCallback<QList<Track>> callback);
    QString userPath(const QString& rest) const;

    ApiClient* apiClient;
    Account accountData;
    QSet<QString> likedIds;
    QSet<QString> stationFeedbackSessions;
};

}  // namespace Yandex
