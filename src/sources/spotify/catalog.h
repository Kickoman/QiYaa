#pragma once

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

class Catalog : public QObject {
    Q_OBJECT
public:
    Catalog(Core::Player* player, Library* library, QObject* parent = nullptr);

    Library* library() const { return spotifyLibrary; }

    void playLikes();
    void playPlaylist(const Item& playlist);
    void playAlbum(const Item& album);
    void playArtist(const Item& artist);
    void search(const QString& text);
    void playRadio(const Core::Track& track);

    bool owns(const Core::TrackKey& key) const;

Q_SIGNALS:
    void noticed(const Core::SourceNotice& notice);

private:
    using TTracksDone = Library::TDone<QList<Core::Track>>;

    TTracksDone queueLoader(const QString& title, const QString& contextUri, quint64 ticket, bool counted);
    void notify(const Core::SourceNotice& notice);

    Core::Player* corePlayer;
    Library* spotifyLibrary;
};

}  // namespace SpotifySource
