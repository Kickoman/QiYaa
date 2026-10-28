#pragma once

#include "core/queue_source.h"
#include "core/source_notice.h"
#include "core/track.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace Core {

struct SearchPick {
    enum class Kind { Artist, Album };

    Kind kind = Kind::Artist;
    QString id{};
    QString name{};
    QStringList artists{};
    int year = 0;
    bool single = false;
};

struct SearchResults {
    std::optional<Track> bestTrack{};
    std::optional<SearchPick> bestArtist{};
    std::optional<SearchPick> bestAlbum{};
    QList<Track> tracks{};
    QList<SearchPick> artists{};
    QList<SearchPick> albums{};

    bool isEmpty() const;
    QList<Track> playable() const;
};

SearchResults Shown(SearchResults results);
SourceNotice AddedNotice(const QString& title, const QList<Track>& tracks, AddPlacement placement);

class SearchCatalog {
public:
    using TFound = std::function<void(const SearchResults& results, const QString& error)>;
    using TTracks = std::function<void(const QList<Track>& tracks, const QString& error)>;
    using TFrom = std::optional<SearchPick>;

    virtual ~SearchCatalog() = default;

    virtual bool canSearch() const = 0;
    virtual void find(const QString& text, TFound done) = 0;
    virtual void open(const SearchPick& pick, TTracks done) = 0;
    virtual void playPick(const SearchPick& pick) = 0;
    virtual void playTracks(const QString& title, const QList<Track>& tracks, int start, const TFrom& from) = 0;
    virtual void add(const QString& title, const QList<Track>& tracks, AddPlacement placement) = 0;
    virtual void addAlbum(const SearchPick& album, AddPlacement placement) = 0;
};

}  // namespace Core
