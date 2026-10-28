#pragma once

#include "core/search.h"
#include "core/source_notice.h"
#include "core/track.h"
#include "sources/spotify/library.h"

#include <QList>
#include <QObject>
#include <QString>

namespace Core {
class Player;
}  // namespace Core

namespace SpotifySource {

class Catalog : public QObject, public Core::SearchCatalog {
    Q_OBJECT
public:
    Catalog(Core::Player* player, Library* library, QObject* parent = nullptr);

    Library* library() const { return spotifyLibrary; }

    void playLikes();
    void playPlaylist(const Item& playlist);
    void playAlbum(const Item& album);
    void playArtist(const Item& artist);
    void playRadio(const Core::Track& track);

    void setLoggedIn(bool on) { loggedIn = on; }
    bool canSearch() const override;
    void find(const QString& text, TFound done) override;
    void open(const Core::SearchPick& pick, TTracks done) override;
    void playPick(const Core::SearchPick& pick) override;
    void playTracks(const QString& title, const QList<Core::Track>& tracks, int start, const TFrom& from) override;
    void add(const QString& title, const QList<Core::Track>& tracks, Core::AddPlacement placement) override;
    void addAlbum(const Core::SearchPick& album, Core::AddPlacement placement) override;

    bool owns(const Core::TrackKey& key) const;

Q_SIGNALS:
    void noticed(const Core::SourceNotice& notice);

private:
    using TTracksDone = Library::TDone<QList<Core::Track>>;

    TTracksDone queueLoader(const QString& title, const QString& contextUri, quint64 ticket, bool counted);
    void notify(const Core::SourceNotice& notice);

    Core::Player* corePlayer;
    Library* spotifyLibrary;
    bool loggedIn = false;
};

}  // namespace SpotifySource
