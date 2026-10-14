# `src/sources/yandex` — Yandex Music as a source

Yandex Music as one of the [music sources](../README.md). `Catalog` turns what the user picks
(likes, playlists, artists, albums, vibes and stations, search) into the `Core::Player`'s queue,
builds a vibe's load-more and feedback callbacks, and likes and dislikes tracks; it is the desktop
counterpart of the Android app's queue manager, and `spec/player` describes both. `Source` is
Yandex's `Core::TrackSource`: it opens a track's file in the chosen quality and reports a play.
`JamWave` is the
Yandex half of the jam's own vibe: the rotor session, its seeds and its feedback, behind
`Core::JamWave`. The module does not speak HTTP or parse the API
([src/yandex](../../yandex/README.md)), does not hold the queue or decide when to ask for more
([src/core](../../core/README.md)), and builds no menus ([src/ui](../../ui/README.md),
`library_menu.cpp`). Namespace `YandexSource`, library `qiyaa_sources_yandex`.

| File | Contains |
|---|---|
| `catalog.h/.cpp` | `Catalog`: playing a pick (likes, playlist, recommendations, artist, album, vibe or station, search) with a ticket each, like and dislike by track key |
| `vibe.h/.cpp` | `Vibe`, the `Core::Continuation` of a vibe or station: `radioStarted`, load-more, the feedback on its tracks |
| `history_wave.h/.cpp` | `HistoryWave`, the vibe that goes on after a queue's head, from the tracks that started; `HistoryWaves`, its factory for `Core::Player`; `HistorySeeds` |
| `wave_origins.h/.cpp` | `WaveOrigins`: the session, station and batch each wave track came in, and the feedback sent to them |
| `jam_wave.h/.cpp` | `JamWave` (a `Core::JamWave`) and `JamWaves`, the factory `Core::JamMode` takes |
| `source.h/.cpp` | `Source`, the `Core::TrackSource` of Yandex tracks: the cover, the web page, opening the file, the play report, the quality; `FailureKindOf` |
| `tracks.h/.cpp` | `kSourceName`, `KeyOf`, `TrackOf`, `TracksOf`, `YandexTrackOf`, `CatalogId`: `Core::Track` ↔ `Yandex::Track`; `WaveEventOf` |

## Dependencies

- `qiyaa_sources_yandex` links PUBLIC `qiyaa_core`, `qiyaa_yandex` and `Qt6::Core`, nothing PRIVATE.
- `jam`, `ui` and `app` link it ([docs/architecture.md](../../../docs/architecture.md#modules)).
- It includes only `core/`, `yandex/` and its own folder.
- `Yandex::Track` stays here, in the client and in the jam's protocol translation
  (`jam/host_session`); everything else sees `Core::Track`.

```bash
grep -rn --include='*.cpp' --include='*.h' '#include "' src/sources/yandex/ | grep -v '"\(core\|yandex\|sources/yandex\)/'   # must print nothing
grep -rnw --include='*.cpp' --include='*.h' 'throw\|catch' src/sources/yandex/                                           # must print nothing
grep -rlw --include='*.h' --include='*.cpp' --include='*.mm' 'Yandex::Track' src/ | grep -v '^src/\(yandex\|sources/yandex\)/\|^src/jam/host_session\.\(h\|cpp\)$'   # must print nothing
grep -rl --include='*.h' --include='*.cpp' --include='*.mm' 'reportPlayStarted' src/ | grep -v '^src/yandex/\|^src/sources/yandex/source\.cpp$'   # must print nothing
```

## `catalog.h`: `Catalog`

```cpp
class Catalog : public QObject {
public:
    static inline const QString kMyWaveSeed = "user:onyourwave";
    Catalog(Core::Player* player, Yandex::Library* library, QObject* parent = nullptr);

    void playLikes(bool autoplay);                               // "Liked"
    void playPlaylist(const Yandex::PlaylistReference& playlist);
    void playRecommendations(const Yandex::PlaylistReference& playlist);   // "<title>: similar"
    void playArtist(const QString& artistId, const QString& name);        // top tracks
    void playAlbum(const QString& albumId, const QString& title);
    void playWave(const QStringList& seeds, const QString& title);        // vibes and stations
    void playMyWave();                                           // {kMyWaveSeed}, "My Vibe"
    void search(const QString& text);                            // "Search: <text>"
    void searchTracks(const QString& text,                       // tracks only, for the jam window
                      std::function<void(const QList<Core::Track>&, const Yandex::RequestError&)> callback);

    bool owns(const Core::TrackKey& key) const;                  // a Yandex track
    bool isLiked(const Core::TrackKey& key) const;
    bool isCurrent(const Core::TrackKey& key) const;             // the same Yandex track as the current one
    void setLiked(const Core::TrackKey& key, bool liked);
    void dislikeAndSkip(const Core::TrackKey& key);              // skip only if it is current

    const QStringList& lastWaveSeeds() const;                    // what the wheel of vibes matches
    void setJamMode(const Core::JamMode* jam);                   // HOST-21
Q_SIGNALS:
    void likesChanged();                                         // Library::likesChanged
};
```

The scenarios are [spec/player/sources.md](../../../spec/player/sources.md),
[wave.md](../../../spec/player/wave.md) and [tracking.md](../../../spec/player/tracking.md);
`tests/sources_test.cpp` checks them by ID.

- **One ticket per pick.** Every `play…` and `search` takes `Player::newSourceRequest()` before
  its request, and its reply is applied only if `isLatestSourceRequest(ticket)` (SRC-01, SRC-02).
  A search whose best result is an artist or album asks for that one's tracks with the same
  ticket (SRC-03). Replies are dropped silently; the HTTP request is not aborted. The `Player` is
  held through `QPointer`.
- **Statuses**, all through `Player::statusMessage` (English sources, shown translated in the
  context `YandexSource::Catalog`; see [translations](../../../translations/README.md)):
  `<title>: loading…` (vibes, likes), `Search: <text>…`, `Error: …`, `Vibe error: …`,
  `Search error: …`, `<title>: empty`, `Nothing found`, `Liked: N tracks`, `Added to Liked`,
  `Removed from Liked`, `Disliked`, `A jam is on: add tracks to the jam`.
- **An empty pick keeps the queue** (SRC-07, SRC-08, WAVE-03): when a reply has no available
  track (none at all, or all `available: false`), the status is `<title>: empty` (search:
  `Nothing found`) and nothing else happens: no `setSource`, and for a vibe no `RadioStarted`.
- **Search** (SRC-09 to SRC-12): a best artist queues its top 100 under its name, a best album its
  tracks under its title, anything else the found tracks as `Search: <text>`.
- **A vibe** (`playWave`): `Library::startWave(seeds)`. On success the source is an empty head
  and a [`Vibe`](#vibeh-vibe) with the first batch as its tracks; the seeds become
  `lastWaveSeeds()`.
- **Tracks** go to the `Player` as `Core::Track`s (`TracksOf`). The vibe's history, the batch of
  each track and every like and dislike use the catalog id (`CatalogId`); the feedback's
  `trackId` is `<id>:<albumId>` as before.
- **Likes**: `setLiked` and `dislikeAndSkip` call the `Library` with the catalog id and report in
  the status line; another source's key is never liked (`isLiked` is false, `owns` tells the menu).
  `dislikeAndSkip` calls `Player::next()` at once only when the key is the current Yandex track
  (`isCurrent`), which closes it with `Skip` (TRK-07). Disliking another track leaves playback
  alone, including when playback advances while a menu for the previous track is open.
- **During a jam** (`setJamMode`): while the jam is on, every pick (likes, playlists, artists,
  albums, vibes, stations, search) leaves the queue alone and says
  `A jam is on: add tracks to the jam` (HOST-21). Likes and dislikes still work (HOST-17).

**Traps:**
- One Yandex track can have two keys: a guest's jam item without an album is `11`, the same track
  from the host's catalogue `11:1`. Compare `CatalogId`s where Yandex's identity matters, keys
  where the queue's does.
- A `Vibe` holds the `Library`, not the `Catalog`: the `Player` closes the open track, and sends its
  feedback, while the application shuts down, after the `Catalog` may be gone.
- `playLikes(false)` (after login) fills the queue without starting playback.

## `vibe.h`: `Vibe`

```cpp
class Vibe : public Core::Continuation, public std::enable_shared_from_this<Vibe> {
public:
    Vibe(Yandex::Library* library, const Yandex::WaveBatch& first, QString station);
};
```

A vibe or station as a queue's continuation (spec/player/wave.md, tracking.md):
- `begin(true)` sends `RadioStarted` with the first batch, once; the `Player` calls it when it takes
  the source, before the first track's `trackStarted` (TRK-03).
- `more` asks `Library::moreWave` of its session with the catalog ids of the request's `queueEnd`,
  the last 5 tracks of the queue (WAVE-05). A failed request only logs `wave: …`; the reply has no
  tracks then.
- `trackEvent` sends the rotor feedback (`WaveEventOf`) of a track it gave to the session, station
  and batch it came in (TRK-04 to TRK-07). The `Player` gives it only its own tracks.
- The library callback holds the `Vibe` (`shared_from_this`): a reply for a queue that was replaced
  meanwhile reaches a live object, and the `Player` drops it.

## `history_wave.h`: `HistoryWave`, `HistoryWaves`

```cpp
inline constexpr int kHistorySeeds = 5;
QStringList HistorySeeds(const QList<Core::Track>& history);   // track:<id>, Yandex only, each once
class HistoryWave : public Core::Continuation, public std::enable_shared_from_this<HistoryWave> {
public:
    explicit HistoryWave(Yandex::Library* library);
};
Core::THistoryWaveFactory HistoryWaves(Yandex::Library* library);   // what Core::Player takes
```

The vibe from history (spec/player/continuation.md, CONT-04 to CONT-08), one per queue:
- Without a session, `more` takes the seeds from the request's `history` (`HistorySeeds`: the
  Yandex tracks among them, `track:<catalog id>` without an album, each id once, at most 5, in the
  order given). Without any it returns `false` and asks nothing (CONT-06). Otherwise
  `Library::startWave(seeds)`: an error gives `done` with that `failure` and no session; a batch
  with no available track gives `done({})` and no session either, so the next `more` starts again
  (CONT-07). A batch with tracks opens the session (the station is the first seed), sends
  `RadioStarted` when the request has `feedback` (TRK-03), and gives `done` the batch.
- With a session, `more` is `Library::moreWave` with the catalog ids of `queueEnd`, as `Vibe`'s.
- `trackEvent` sends the feedback of a track it gave, through its `WaveOrigins`; the head's tracks
  send none (CONT-08).
- The library callbacks hold the `HistoryWave` (`shared_from_this`).

`WaveOrigins` (`wave_origins.h`) is the shared part of `Vibe`, `HistoryWave` and `JamWave`: for each
track id of a batch, the session, station and batch it came in, and `send`, which posts a track's
feedback there and nothing for a track it does not know.

## `jam_wave.h`: `JamWave`, `JamWaves`

```cpp
class JamWave : public Core::JamWave, public std::enable_shared_from_this<JamWave> {
public:
    JamWave(Yandex::Library* library, bool feedback, TRecent recent);
};
Core::TJamWaveFactory JamWaves(Yandex::Library* library);     // what Core::JamMode takes
```

What `Core::JamMode` asks of the jam's vibe is in
[src/core](../../core/README.md#jam_modeh-jammode); this is how Yandex answers it (spec/jam/host.md,
HOST-11, HOST-13, HOST-16):

- `start(seeds, seedsVersion, current, done)`: `Library::startWave` with `seeds`; without seeds
  `track:<catalog id of current>` when the current track is a Yandex one; without either it
  returns false and asks nothing. A reply with an error or
  without an available track opens no session and gives `done({})`. Otherwise the session, the
  station (the first seed) and `seedsVersion` are kept, `RadioStarted` goes out when `feedback`,
  and `done` gets the batch.
- `more(done)`: `Library::moreWave` of the open session with the catalog ids of `recent()`, the
  last wave tracks of the jam's queue. Both give `done` the batch as `Core::Track`s.
- Every batch records, for each of its track ids, the session, station and batch it came in, so a
  track's feedback goes to its own session even after a new one started (TRK-08).
- `trackEvent`: rotor feedback (`WaveEventOf`) for a track of this vibe only (found by catalog id),
  and only when `feedback`.
- A `more` reply for a session that a new `start` replaced while it was asked for gives `done({})`
  and records nothing; sessions are counted, not compared by id, since the service may give a new
  session the old id.
- The library callbacks hold the `JamWave` (`shared_from_this`), as the `Player`'s event callback
  does: a wave track that closes after the jam ended still gets its feedback.

## `tracks.h`: `Core::Track` ↔ `Yandex::Track`

```cpp
inline const QString kSourceName = "yandex";
Core::TrackKey KeyOf(const QString& trackId, const QString& albumId);   // {"yandex", "<track>:<album>" or "<track>"}
Core::Track TrackOf(const Yandex::Track& track);
QList<Core::Track> TracksOf(const QList<Yandex::Track>& tracks);
std::optional<Yandex::Track> YandexTrackOf(const Core::Track& track);  // nullopt: another source's track
QString CatalogId(const Core::TrackKey& key);                          // the track id alone; empty for another source
Yandex::WaveEvent WaveEventOf(Core::Player::TrackEvent event);   // Started, Finished, Skipped → TrackStarted, TrackFinished, Skip
```

- The key's id is Yandex's own `<track id>:<album id>` form, the one `download-info` and the rotor
  feedback take; without an album it is the track id alone. A Yandex id never holds `:`, so
  `YandexTrackOf` splits at the first one.
- `TrackOf` keeps every field: `album` is `albumTitle`, `cover` is the raw `coverUri` template
  (`%%` for the size). `YandexTrackOf(TrackOf(t))` is `t` (`everySpecTrackComesBackWhole`).
- What wants the track id alone takes `CatalogId`: likes and dislikes, the vibe's history, the
  `track:<id>` seed, the origin of a vibe's tracks, a jam item's id.

## `source.h`: `Source`

```cpp
class Source : public Core::TrackSource {
public:
    explicit Source(Yandex::Library* library, QObject* parent = nullptr);
    QUrl coverUrl(const Core::Track& track, int pixels) const override;   // Yandex::Track::coverUrl
    QUrl webPage(const Core::Track& track) const override;                // Yandex::Track::webUrl
    void open(const Core::Track& track, TOpened done) override;
    void trackEvent(Core::TrackEvent event, const Core::Track& track, double playedSeconds) override;
    void setQuality(Yandex::Quality quality);                             // filesChanged on a change
    Yandex::Quality quality() const;                                      // Standard until set
};

Core::FailureKind FailureKindOf(const Yandex::RequestError& error);      // Network; HTTP 401/403 Auth; else Track
```

`src/app` adds it to its `Core::SourceRegistry` under `kSourceName`, restores the quality from the
settings and changes it from the menu.

- **Cover and page** come from the client ([src/yandex](../../yandex/README.md)): the avatars URL
  with `%%` replaced by `<pixels>x<pixels>`, and
  `https://music.yandex.ru/album/<album>/track/<track>` (without an album, `…/track/<track>`).
  Another source's track gets empty URLs.
- **`open`** resolves the link of the key's file (`ApiClient::resolveTrackUrl` with the key's id,
  in `quality()`), then answers with the link, its bitrate and codec, and a download that sends
  its GET (`ApiClient::download`) only on `start()`. A failure is a `TrackFailure` of
  `FailureKindOf` with the client's text; so is a failed download's.
- **`trackEvent`**: `Started` posts `/play-audio` with the account, the key's id, the duration and
  a new UUID as the play id. `Finished` and `Skipped` send nothing. The `Player` leaves out the
  tracks of a jam (HOST-15).
- **`setQuality`** applies from the next link on: the playing track goes on with its file, and
  `filesChanged` makes the `Player` fetch its preload again (QUAL-05).
- The `Library` must outlive the `Source` and every download it opened.
- Tests: `tests/yandex_source_test.cpp`.

## Not here

- The rotor endpoints, the feedback's JSON and the track model:
  [src/yandex](../../yandex/README.md).
- When the queue asks for more, the jam's slots, and when its vibe starts or goes on:
  [src/core](../../core/README.md).
- The Yandex menu that calls `Catalog` and the status line: [src/ui](../../ui/README.md).
- Tests: `tests/yandex_source_test.cpp` (keys, the `Source`), `tests/sources_test.cpp` (the
  `Catalog`), `tests/jam_mode_test.cpp` and `tests/jam_host_test.cpp` (the jam's vibe through
  `Core::JamMode`).
