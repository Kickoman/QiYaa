# `src/sources/yandex` — Yandex Music as a source

Yandex Music as one of the [music sources](../README.md). `Catalog` turns what the user picks
(likes, playlists, artists, albums, vibes and stations, search) into the `Core::Player`'s queue,
builds a vibe's load-more and feedback callbacks, and likes and dislikes tracks; it is the desktop
counterpart of the Android app's queue manager, and `spec/player` describes both. `JamWave` is the
Yandex half of the jam's own vibe: the rotor session, its seeds and its feedback, behind
`Core::JamWave`. The module does not speak HTTP or parse the API
([src/yandex](../../yandex/README.md)), does not hold the queue or decide when to ask for more
([src/core](../../core/README.md)), and builds no menus ([src/ui](../../ui/README.md),
`library_menu.cpp`). Namespace `YandexSource`, library `qiyaa_sources_yandex`.

| File | Contains |
|---|---|
| `catalog.h/.cpp` | `Catalog`: playing a pick (likes, playlist, recommendations, artist, album, vibe or station, search) with a ticket each, the vibe's load-more and feedback callbacks, like and dislike by track ID |
| `jam_wave.h/.cpp` | `JamWave` (a `Core::JamWave`) and `JamWaves`, the factory `Core::JamMode` takes |
| `tracks.h/.cpp` | `WaveEventOf`: a `Player` track event as rotor feedback |

## Dependencies

- `qiyaa_sources_yandex` links PUBLIC `qiyaa_core`, `qiyaa_yandex` and `Qt6::Core`, nothing PRIVATE.
- `jam`, `ui` and `app` link it ([docs/architecture.md](../../../docs/architecture.md#modules)).
- It includes only `core/`, `yandex/` and its own folder.

```bash
grep -rn --include='*.cpp' --include='*.h' '#include "' src/sources/yandex/ | grep -v '"\(core\|yandex\|sources/yandex\)/'   # must print nothing
grep -rnw --include='*.cpp' --include='*.h' 'throw\|catch' src/sources/yandex/                                           # must print nothing
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

    void setLiked(const QString& trackId, bool liked);
    void dislikeAndSkip(const QString& trackId);                 // skip only if the ID is current

    const QStringList& lastWaveSeeds() const;                    // what the wheel of vibes matches
    void setJamMode(const Core::JamMode* jam);                   // HOST-21
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
  `Nothing found`) and nothing else happens: no `setQueue`, and for a vibe no `RadioStarted`.
- **Search** (SRC-09 to SRC-12): a best artist queues its top 100 under its name, a best album its
  tracks under its title, anything else the found tracks as `Search: <text>`.
- **A vibe** (`playWave`): `Library::startWave(seeds)`. On success the vibe's state (session,
  station = first seed, track id → batch id) is shared by two callbacks for the life of that
  queue: `more` asks `moreWave` with the ids of the last 5 queued tracks (WAVE-05), and `events`
  maps each `TrackEvent` with `WaveEventOf` and sends it with the batch the track came in (TRK-04
  to TRK-06). `RadioStarted` is sent before `setQueue`, so before the first track's
  `trackStarted` (TRK-03). The seeds become `lastWaveSeeds()`.
- **Likes**: `setLiked` and `dislikeAndSkip` call the `Library` and report in the status line;
  `dislikeAndSkip` calls `Player::next()` at once only when `trackId` matches the current track,
  which closes it with `Skip` (TRK-07). Disliking another track leaves playback alone, including
  when playback advances while a menu for the previous track is open.
- **During a jam** (`setJamMode`): while the jam is on, every pick (likes, playlists, artists,
  albums, vibes, stations, search) leaves the queue alone and says
  `A jam is on: add tracks to the jam` (HOST-21). Likes and dislikes still work (HOST-17).

**Traps:**
- The vibe's callbacks hold the `Library` and a `QPointer<Player>`, not the `Catalog`: the
  `Player` closes the open track, and sends its feedback, while the application shuts down,
  after the `Catalog` may be gone.
- `playLikes(false)` (after login) fills the queue without starting playback.

## `jam_wave.h`: `JamWave`, `JamWaves`

```cpp
class JamWave : public Core::JamWave, public std::enable_shared_from_this<JamWave> {
public:
    JamWave(Yandex::Library* library, bool feedback, TRecent recent);
};
Core::TJamWaveFactory JamWaves(Yandex::Library* library);     // what Core::JamMode takes
```

What `Core::JamMode` asks of the jam's vibe is in [src/core](../../core/README.md#jam_modeh-jammode);
this is how Yandex answers it (spec/jam/host.md, HOST-11, HOST-13, HOST-16):

- `start(seeds, seedsVersion, current, done)`: `Library::startWave` with `seeds`; without seeds
  `track:<current id>`; without either it returns false and asks nothing. A reply with an error or
  without an available track opens no session and gives `done({})`. Otherwise the session, the
  station (the first seed) and `seedsVersion` are kept, `RadioStarted` goes out when `feedback`,
  and `done` gets the batch.
- `more(done)`: `Library::moreWave` of the open session with the ids of `recent()`, the last
  wave tracks of the jam's queue.
- Every batch records, for each of its track ids, the session, station and batch it came in, so a
  track's feedback goes to its own session even after a new one started (TRK-08).
- `trackEvent`: rotor feedback (`WaveEventOf`) for a track of this vibe only, and only when
  `feedback`.
- `endSession()` forgets the session: the next load starts one. A reply still in flight records
  its tracks under the session as it is when the reply comes.
- The library callbacks hold the `JamWave` (`shared_from_this`), as the `Player`'s event callback
  does: a wave track that closes after the jam ended still gets its feedback.

## `tracks.h`: `WaveEventOf`

```cpp
Yandex::WaveEvent WaveEventOf(Core::Player::TrackEvent event);   // Started, Finished, Skipped → TrackStarted, TrackFinished, Skip
```

## Not here

- The rotor endpoints, the feedback's JSON and the track model: [src/yandex](../../yandex/README.md).
- When the queue asks for more, the jam's slots, and when its vibe starts or goes on:
  [src/core](../../core/README.md).
- The Yandex menu that calls `Catalog` and the status line: [src/ui](../../ui/README.md).
- Tests: `tests/sources_test.cpp` (the `Catalog`), `tests/jam_mode_test.cpp` and
  `tests/jam_host_test.cpp` (the jam's vibe through `Core::JamMode`).
