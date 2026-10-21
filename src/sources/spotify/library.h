#pragma once

#include "core/track.h"
#include "spotify/protocol/bytes.h"
#include "spotify/protocol/metadata.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace SpotifyClient {
class Session;
class Web;
}  // namespace SpotifyClient

namespace SpotifySource {

struct Item {
    QString uri;
    QString name;
};

struct ContextPage {
    QList<Core::Track> tracks;
    QString next;
};

struct Found {
    enum class Kind { Artist, Album, Tracks };
    Kind kind = Kind::Tracks;
    Item best;
    QStringList trackUris;
};

class Library : public QObject {
    Q_OBJECT
public:
    template <typename T>
    using TDone = std::function<void(const T& result, const QString& error)>;

    static constexpr int kMetadataBatch = 100;
    static constexpr int kCollectionPage = 300;
    static constexpr int kListPage = 1000;
    static constexpr int kSearchLimit = 50;
    static constexpr int kTopTracks = 10;
    static constexpr int kForYouLimit = 20;

    Library(SpotifyClient::Web* web, SpotifyClient::Session* session, QObject* parent = nullptr);

    QString username() const;

    void tracks(const QStringList& uris, TDone<QList<Core::Track>> done);
    void likedTracks(TDone<QList<Core::Track>> done);
    void playlistTracks(const QString& uri, TDone<QList<Core::Track>> done);
    void albumTracks(const QString& uri, TDone<QList<Core::Track>> done);
    void artistTopTracks(const QString& uri, TDone<QList<Core::Track>> done);
    void playlists(TDone<QList<Item>> done);
    void savedAlbums(TDone<QList<Item>> done);
    void followedArtists(TDone<QList<Item>> done);
    void forYou(TDone<QList<Item>> done);
    void search(const QString& text, TDone<Found> done);
    void station(const QString& trackUri, TDone<ContextPage> done);
    void autoplay(const QString& contextUri, const QStringList& recentTrackUris, TDone<ContextPage> done);
    void nextPage(const QString& pageUrl, TDone<ContextPage> done);

private:
    using TEntities = QHash<QString, SpotifyProtocol::TBytes>;

    QString unreadable();
    void metadata(const QStringList& uris, SpotifyProtocol::ExtensionKind kind, TDone<TEntities> done);
    void collection(const QString& set, TDone<QStringList> done);
    void collectionPage(const QString& set, const QString& token, QStringList uris, TDone<QStringList> done);
    void listUris(const QString& path, int from, QList<Item> items, TDone<QList<Item>> done);
    void names(const QStringList& uris, SpotifyProtocol::ExtensionKind kind, TDone<QList<Item>> done);
    void pathfinder(
        const QString& operation,
        const QStringList& hashes,
        const QJsonObject& variables,
        int attempt,
        TDone<QJsonObject> done
    );
    void context(const QByteArray& method, const QString& path, const QByteArray& body, TDone<ContextPage> done);

    SpotifyClient::Web* web;
    SpotifyClient::Session* session;
};

}  // namespace SpotifySource
