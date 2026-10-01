#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace Yandex {
class Library;
struct PlaylistReference;
}  // namespace Yandex

namespace Core {

class Player;

// What the user picks to listen to, turned into the Player's queue: likes, playlists, artists,
// albums, waves and stations, search. Takes a source-request ticket for each pick, so the last
// pick wins; builds the wave's load-more and feedback callbacks; likes and dislikes the current
// track. Reports through Player::statusMessage.
class Sources : public QObject {
    Q_OBJECT
public:
    static inline const QString kMyWaveSeed = QStringLiteral("user:onyourwave");

    Sources(Player* player, Yandex::Library* library, QObject* parent = nullptr);

    void playLikes(bool autoplay);
    void playPlaylist(const Yandex::PlaylistReference& playlist);
    void playRecommendations(const Yandex::PlaylistReference& playlist);
    void playArtist(const QString& artistId, const QString& name);
    void playAlbum(const QString& albumId, const QString& title);
    void playWave(const QStringList& seeds, const QString& title);
    void playMyWave();
    void search(const QString& text);

    void setLiked(const QString& trackId, bool liked);
    void dislikeAndSkip(const QString& trackId);

    // The seeds of the last wave started, My Wave before any: what the wheel of waves matches.
    const QStringList& lastWaveSeeds() const { return waveSeeds; }

private:
    void showStatus(const QString& text);

    Player* corePlayer;
    Yandex::Library* yandexLibrary;
    QStringList waveSeeds{kMyWaveSeed};
};

}  // namespace Core
