# `src/sources/spotify` — Spotify as a source

Spotify as one of the [music sources](../README.md), described by
[spec/player/spotify.md](../../../spec/player/spotify.md). The protocol is
[src/spotify/protocol](../../spotify/protocol/README.md) and the Qt client (login, access point, tokens, downloads)
[src/spotify/client](../../spotify/client/README.md); this module turns them into what the player knows:

- `Library` asks Spotify for the picks and for track metadata, and answers with `Core::Track`s and named items;
- `Catalog` turns a pick into the `Core::Player`'s queue, with the statuses;
- `Autoplay` is the continuation of a pick (Spotify's autoplay, under the flag) and of a radio (its station);
- `Source` is the `Core::TrackSource` of Spotify tracks: the file to play, the cover, the web page.

It shows no dialog and holds no menu (those are [src/ui](../../ui/README.md)'s and
[src/app](../../app/README.md)'s) and reports nothing to Spotify (SPOT-19). Namespace `SpotifySource`, library
`qiyaa_sources_spotify`.

| File | Contains |
|---|---|
| `tracks.h/.cpp` | `kSourceName` (`spotify`), `TrackOf`, `UriOf`, `IdOfUri`, `GidOf`, `TrackUriOf`, `CoverUrlOf`, `WebPageOf`: a Spotify track as a `Core::Track` and back |
| `library.h/.cpp` | `Library`, `Item`, `ContextPage`, `Found`: the picks and the metadata over `SpotifyClient::Web` |
| `catalog.h/.cpp` | `Catalog`: a pick, a search or a radio into the player's queue, with a ticket each, and the statuses |
| `autoplay.h/.cpp` | `Autoplay`: a `Core::Continuation` over Spotify's autoplay or a track's station, page by page |
| `source.h/.cpp` | `Source`, the `Core::TrackSource` of Spotify tracks: opening a file in the quality in effect |

## Dependencies

- `qiyaa_sources_spotify` links PUBLIC `qiyaa_core`, `qiyaa_spotify_protocol` (`tracks.h` and `library.h` take its
  types) and `Qt6::Core`, PRIVATE `qiyaa_spotify_client` (the headers only forward-declare `Session` and `Web`).
  `ui` and `app` link it; nothing else does.
- It includes only `core/`, `spotify/` and its own folder.

```bash
grep -rn --include='*.cpp' --include='*.h' '#include "' src/sources/spotify/ | grep -v '"\(core\|spotify\|sources/spotify\)/'   # must print nothing
grep -rnw --include='*.cpp' --include='*.h' 'throw\|catch' src/sources/spotify/                                         # must print nothing
```

## `tracks.h`

- A track's key is `{"spotify", <base62 id>}`; its URI is `spotify:track:<id>`.
- `TrackOf` takes the name, the artists, the album's name and year, the duration and the album's covers. A track is
  `available` when it has an Ogg Vorbis file of its own or through an alternative (SPOT-15): Spotify sends a track
  the account's country may not play without files, relinked to an alternative that has them.
- `Track::cover` holds every cover as `<hex id>@<pixels>,…`. `CoverUrlOf(track, pixels)` is the smallest one at least
  as large as `pixels`, else the largest, at `https://i.scdn.co/image/<hex id>`; `WebPageOf` is
  `https://open.spotify.com/track/<id>` (SPOT-19).

## `library.h`: `Library`

```cpp
template <typename T> using TDone = std::function<void(const T& result, const QString& error)>;
void tracks(const QStringList& uris, TDone<QList<Core::Track>> done);   // TRACK_V4, in the order of uris
void likedTracks(TDone<QList<Core::Track>>);                            // SPOT-07
void playlistTracks(const QString& uri, …); void albumTracks(…); void artistTopTracks(…);
void playlists(TDone<QList<Item>>); void savedAlbums(…); void followedArtists(…); void forYou(…);
void search(const QString& text, TDone<Found> done);                    // SPOT-11
void station(const QString& trackUri, TDone<ContextPage>);              // SPOT-12
void autoplay(const QString& contextUri, const QStringList& recentTrackUris, TDone<ContextPage>);  // SPOT-20
void nextPage(const QString& pageUrl, TDone<ContextPage>);              // hm://… of a ContextPage
```

- Every call answers once, with the result or an English error text, on the GUI thread. Nothing is cached: each
  call asks Spotify again.
- Track metadata is asked `kMetadataBatch` (100) URIs at a time, all at once, and matched by URI. A track Spotify
  does not answer for (status other than 200) is left out; an episode or a local file of a playlist never is asked.
- Liked Songs are the `spotify:track:` items of the `collection` set, saved albums its `spotify:album:` items,
  followed artists the `artist` set (SPOT-07, SPOT-09), in Spotify's order (newest first), `kCollectionPage` (300) a
  page. An album plays its tracks disc by disc; an artist its top tracks in the session's country, else the
  worldwide ones, at most `kTopTracks` (10).
- Playlists are the rootlist's, folders flattened, and a playlist's tracks are read `kListPage` (1000) a page
  (SPOT-08).
- "For you" is the "Made For You" section (`spotify:section:0JQ5DAUnp4wcj0bCb3wh3S`) of Pathfinder's `home`
  (SPOT-10); search is Pathfinder's `searchDesktop`, `kSearchLimit` (50) of each part. The best result is the first
  top result when it is an artist, an album or a track (`Found::Kind`); `Found` also lists the `spotify:artist:`
  items of `artists` and the `spotify:album:` items of `albumsV2` (`FoundAlbum`: the name, the artists, `date.year`,
  `type` `SINGLE`), each item through `item` when it has one and then `data` (SPOT-11). The old `search` plays a
  best artist or album and anything else the found tracks.
- Pathfinder queries are persisted: only a hash of the query is sent. A list of known hashes is tried newest first;
  a 400, 405, 412, or a reply without `data` moves to the next one.
- An answer it cannot read, and Pathfinder refusing every hash, are also reported through
  `SpotifyClient::Web::report` (`Content`, `QueryOutdated`) for the telemetry.
- A station, autoplay and their pages are JSON contexts: the `spotify:track:` URIs of the first page, then their
  metadata; `ContextPage::next` is the page after it, empty at the end.

## `catalog.h`: `Catalog`

- Each pick takes a ticket from `Player::newSourceRequest()`; an answer to an older ticket is dropped, as Yandex's
  (SRC-01 to SRC-08). A pick with no available track says `Empty`; a failure says `Failed` (`SearchFailed` for a
  search, `VibeFailed` for a radio). Liked Songs say `Loading` first and `Loaded` with the count after.
- A pick's queue is its tracks as the head and `Autoplay::Of(<the pick's URI>)` as `QueueSource::historyWave`: the
  liked songs' `spotify:user:<username>:collection`, the playlist's, the album's or the artist's own URI, and for
  found tracks the first one's (SPOT-20).
- As `Core::SearchCatalog` (SRC-13 to SRC-19): `find` is `search` and then the metadata of the found tracks, the
  best track first, into `Core::SearchResults` (`Core::Shown`); a pick's id is its URI. `playTracks` queues the tracks
  with `Autoplay::Of` the opened artist's or album's URI, or for found tracks the first one's (SPOT-20); `add` gives
  a queue it starts in an empty one the first available added track's URI (ADD-05). `canSearch` is what the app last
  passed to `setLoggedIn`: the account's state, false after a refused login.
- `playRadio(track)` (SPOT-12) queues the track's station as the continuation of an empty head, titled
  "<title>: radio"; the flag changes nothing for it.

## `autoplay.h`: `Autoplay`

- `Autoplay::Of(library, contextUri)`: the first request is `POST /context-resolve/v1/autoplay` with the context and
  `RecentUris(history)`: the Spotify tracks of the queue that started, oldest first, at most `kRecentTracks` (50).
  `Autoplay::Station(library, trackUri, nextPage)` goes on from the station's next page.
- Each `more` asks the page after the last one; a page without a next one asks anew (autoplay with the history of
  then, or the station again: Spotify makes a new mix each time). A failure is a reply with `failure` and no tracks
  (SPOT-21, CONT-07).
- It does not follow a jam's seeds (`followsJamSeeds()` is false, SPOT-22) and sends no feedback.

## `source.h`: `Source`

- `open` (SPOT-13): without an account (`Session::hasAccount()` false) an `Auth` failure at once (SPOT-06); else a
  `SpotifyClient::FileDownload` is prepared (metadata, file, key, links) and handed over as the `TrackDownload`,
  with the file's bitrate. Its failures map to `Network`, `Track` and, for a refused key, `Auth` (SPOT-17).
- `setEconomy(true)` asks for 96 kbps, else 320 (`kEconomyKbps`, `kStandardKbps`); the fallback to 160 and to any
  Ogg Vorbis is the protocol's `PickOggVorbis`.
- `trackEvent` reports nothing and `lyrics` answers `Missing`: Spotify's own lyrics are not asked.

## Traps

- The account's country comes from the access point a moment after the login. An artist's top tracks asked before
  it are the worldwide ones.
- Pathfinder's hashes go stale when Spotify updates its clients. When every known one fails, "For you" and search
  fail until the list in `library.cpp` is updated (librespot-based clients publish the new ones).
- `Autoplay` keeps its `Library` as a `QPointer`: with the library gone, `more` returns false.
- Tests: `tests/spotify_source_test.cpp`, and `player_test` for the own history wave.
