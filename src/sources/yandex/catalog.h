#pragma once

#include "core/search.h"
#include "core/source_notice.h"
#include "core/track.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace Core {
class Player;
}  // namespace Core

namespace Yandex {
class Library;
struct PlaylistReference;
struct RequestError;
struct Track;
}  // namespace Yandex

namespace YandexSource {

class Catalog : public QObject, public Core::SearchCatalog {
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
    void searchTracks(
        const QString& text,
        std::function<void(const QList<Core::Track>&, const Yandex::RequestError&)> callback
    );

    bool canSearch() const override;
    void find(const QString& text, TFound done) override;
    void open(const Core::SearchPick& pick, TTracks done) override;
    void playPick(const Core::SearchPick& pick) override;
    void playTracks(const QString& title, const QList<Core::Track>& tracks, int start, const TFrom& from) override;
    void add(const QString& title, const QList<Core::Track>& tracks, Core::AddPlacement placement) override;
    void addAlbum(const Core::SearchPick& album, Core::AddPlacement placement) override;

    bool owns(const Core::TrackKey& key) const;
    bool isLiked(const Core::TrackKey& key) const;
    bool isCurrent(const Core::TrackKey& key) const;
    void setLiked(const Core::TrackKey& key, bool liked);
    void dislikeAndSkip(const Core::TrackKey& key);

    const QStringList& lastWaveSeeds() const { return waveSeeds; }

Q_SIGNALS:
    void likesChanged();
    void noticed(const Core::SourceNotice& notice);

private:
    using TTracksDone = std::function<void(const QList<Yandex::Track>& tracks, const QString& error)>;

    TTracksDone queueLoader(const QString& title, quint64 ticket);
    void notify(const Core::SourceNotice& notice);

    Core::Player* corePlayer;
    Yandex::Library* yandexLibrary;
    QStringList waveSeeds{kMyWaveSeed};
};

}  // namespace YandexSource
