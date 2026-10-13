#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace Core {
class JamMode;
class Player;
}  // namespace Core

namespace Yandex {
class Library;
struct PlaylistReference;
}  // namespace Yandex

namespace YandexSource {

class Catalog : public QObject {
    Q_OBJECT
public:
    static inline const QString kMyWaveSeed = QStringLiteral("user:onyourwave");

    Catalog(Core::Player* player, Yandex::Library* library, QObject* parent = nullptr);

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

    const QStringList& lastWaveSeeds() const { return waveSeeds; }

    void setJamMode(const Core::JamMode* jam) { jamMode = jam; }

private:
    void showStatus(const QString& text);
    bool refusedForJam();

    Core::Player* corePlayer;
    Yandex::Library* yandexLibrary;
    QStringList waveSeeds{kMyWaveSeed};
    const Core::JamMode* jamMode = nullptr;
};

}  // namespace YandexSource
