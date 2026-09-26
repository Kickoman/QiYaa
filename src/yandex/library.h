// High-level access to the user's Yandex Music library: likes, playlists,
// artists, albums, stations/waves, search, like/dislike.
// Endpoints mirror what Yaamp used.
#pragma once

#include "yandex/api_client.h"

#include <QHash>
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
    QString id;  // "type:tag", e.g. "genre:rock", "user:onyourwave"
    QString type;  // "genre", "mood", ...
    QString name;
};

struct WaveBatch {
    QString sessionId;
    QString batchId;
    QList<Track> tracks;
};

// An entry of the "wheel" of waves (wave presets with seeds).
struct Wave {
    QString name;
    QString description;
    QStringList seeds;
};

enum class WaveEvent { RadioStarted, TrackStarted, TrackFinished, Skip };

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
    using TCallback = ApiClient::TCallback<T>;

    explicit Library(ApiClient* api, QObject* parent = nullptr);

    ApiClient* api() const { return apiClient; }

    // Loads the account for the API client's current token.
    void connectAccount(TCallback<Account> callback);
    void logout();
    bool isLoggedIn() const { return !accountData.uid.isEmpty(); }
    const Account& account() const { return accountData; }

    void likedTracks(TCallback<QList<Track>> callback);
    void
    tracksByIds(const QStringList& ids, TCallback<QList<Track>> callback);  // chunked, order kept
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

    // "Для вас": Плейлист дня, Дежавю, Премьера, Тайник...
    void personalPlaylists(TCallback<QList<PlaylistRef>> callback);
    void playlistRecommendations(const PlaylistRef& playlist, TCallback<QList<Track>> callback);
    // Waves suggested around `seeds` (e.g. the wave that is playing).
    void wheelWaves(const QStringList& seeds, TCallback<QList<Wave>> callback);

    // Tells the wave what the user did so "Моя волна" learns. Tries the rotor
    // session endpoint first and falls back to the station endpoint for that
    // session if the server rejects it. Fire-and-forget.
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
    // Items that embed track objects, or only ids (then fetched).
    void tracksFromItems(const QJsonArray& items, TCallback<QList<Track>> callback);
    QString userPath(const QString& rest) const;

    ApiClient* apiClient;
    Account accountData;
    QSet<QString> likedIds;
    QSet<QString> stationFeedbackSessions;  // sessions whose session-feedback endpoint failed
};

}  // namespace Yandex
