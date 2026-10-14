# `src/core` — playback controller, jam mode and cover cache

This folder decides what plays. `Player` holds the queue of the current source and moves through it:
play, pause, stop, next, previous, shuffle and repeat. It asks an endless source (a wave) for more
tracks before the queue runs out. For each track it asks the track's source (`TrackSource`, chosen
by `TrackKey::source`) to open it and streams the file's bytes into `Audio::AudioEngine`. It also
preloads the following track, so the engine can go on without a gap, and it reports each track's
start and end: to the source (Yandex's play report) and to a per-queue callback that the wave uses
for feedback. `JamMode` is the queue while the app hosts a jam: the current track, the
jam part that mirrors the server's queue, and the jam wave (`spec/jam/host.md`), which a source
provides through `JamWave`. `CoverCache` downloads album covers and keeps them in memory and on
disk. The folder does not decide what a pick plays or talk to a source's catalogue
([src/sources](../sources/README.md)). It does not decode or output audio or chain streams at the
sample level ([src/audio](../audio/README.md)). It speaks to no music service: links, files, cover
URLs and play reports are a source's ([src/sources](../sources/README.md)). It builds no menus
([src/ui](../ui/README.md), `library_menu.cpp`), shows nothing on screen and holds no text for the
user: the `Player` says what happened as `PlayerNotice` codes, and `src/ui` words them. Namespace `Core`,
library `qiyaa_core`.

| File | Contains |
|---|---|
| `player.h/.cpp` | `Player`: the queue and cursor, transport, shuffle and repeat, source-request tickets, the endless-source hook `TLoadMoreCallback`, track events `TEventCallback`, opening tracks through their sources, download streaming, the gapless preload, and the engine's poll timer |
| `track.h/.cpp` | `Track`, `TrackKey`, `CoverRef`, `TrackEvent`: a track of any source |
| `track_source.h/.cpp` | `TrackSource`, what each music source implements; `TrackDownload`, `OpenedTrack`, `TrackFailure`, `TOpenResult`; `SourceRegistry`, the sources by name |
| `continuation.h` | `Continuation`: an endless supply of tracks and the feedback on them |
| `track_navigator.h/.cpp` | `TrackNavigator`, `ShuffleAlgorithm`, `MakeTrackNavigator`: sequential, random and stored shuffled traversal |
| `failure_policy.h/.cpp` | `FailureKind`, `FailureAction`, `DecideOnFailure`: what the `Player` does about a track that cannot play |
| `jam_mode.h/.cpp` | `JamMode`, `JamEntry`, `JamSlot`, `JamPlayback`, and `JamWave`, what `JamMode` asks of the source of its wave: the jam mode of the queue; it knows nothing of the network or the protocol |
| `cover_cache.h/.cpp` | `CoverCache`: cover images by URL, with an LRU of 30 in memory, files in a cache directory and one download per URL at a time |

## Dependencies

- `qiyaa_core` links PUBLIC `qiyaa_audio`, `Qt6::Gui` (`QImage` in `cover_cache.h`) and
  `Qt6::Network` (`QNetworkAccessManager`, `QNetworkReply` in `cover_cache`). It links nothing
  PRIVATE.
- It deliberately does not link `qiyaa_yandex` (a source's business), `Qt6::Widgets`,
  `qiyaa_skins`, `qiyaa_vis`, or anything above it in the order `audio`, `yandex`, `skins` →
  `vis`, `core` → `sources` → `jam` → `ui`, `integrations` → `app`. It reaches miniaudio only
  through `qiyaa_audio`, which links it PRIVATE. Which modules link `qiyaa_core` is in
  [docs/architecture.md](../../docs/architecture.md#modules).
- `player` and `cover_cache` do not include each other.
- No file includes `yandex/`. The only HTTP of `core` is `CoverCache`'s: a track's file comes
  through its source's `TrackDownload`.
- `Player` is the only caller of `AudioEngine::poll()` in `src/`. Everything the engine reports
  asynchronously therefore arrives through `Player`'s timer (see [Threads and
  lifetime](#threads-and-lifetime)).

```bash
grep -rnE --include='*.h' --include='*.cpp' '#include "(sources|jam|ui|vis|skins|integrations|app)/' src/core/   # must print nothing
grep -rnE --include='*.h' --include='*.cpp' '#include <(QWidget|QApplication|QDialog|QMenu|QtWidgets)' src/core/   # must print nothing
grep -rnwE --include='*.h' --include='*.cpp' 'throw|catch' src/core/                                    # must print nothing
grep -rnE --include='*.h' --include='*.cpp' '\btr\(|translate\(' src/core/                                # must print nothing
grep -rn --include='*.h' --include='*.cpp' -e '->poll()' src/ | grep -v '^src/core/player.cpp:'         # must print nothing
grep -rn --include='*.h' --include='*.cpp' '#include "yandex/' src/core/                               # must print nothing
grep -rnE --include='*.h' --include='*.cpp' 'QNetworkRe(quest|ply)' src/core/ | grep -v '^src/core/cover_cache\.'   # must print nothing
```

## `player.h`: `Player`

```cpp
class Player : public QObject {
public:
    using TLoadMoreCallback = std::function<void(std::function<void(const QList<Track>&)> done)>;
    using TrackEvent = Core::TrackEvent;
    using TEventCallback =
        std::function<void(TrackEvent event, const Track& track, double playedSeconds)>;
    enum class State { Empty, Stopped, Loading, Playing, Paused, WaitingForNetwork, WaitingForMore };

    Player(Audio::AudioEngine* engine, SourceRegistry* sources, QObject* parent = nullptr);

    quint64 newSourceRequest();                       // new ticket; every older one stops being the latest
    bool isLatestSourceRequest(quint64 ticket) const;

    void setQueue(
        const QList<Track>& tracks,
        const QString& title,
        bool autoplay,                                // playIndex(0) right away
        TLoadMoreCallback more = {},                            // set = endless source
        TEventCallback events = {}
    );
    void appendTracks(const QList<Track>& tracks);
    void insertTracks(int index, const QList<Track>& tracks);   // at index, clamped
    void removeTracks(QList<int> indices);            // indices before removal, any order, duplicates ok
    void clearQueue();
    void changeSource(const QString& title, TLoadMoreCallback more, TEventCallback events, QueueRules rules);
    void requestMore();                               // the load-more check now

    void play();                                      // resume / restart while playing / start
    void pause();                                     // toggles
    void stop();
    void next();
    void previous();
    void playIndex(int index);
    void shutDown();                                  // for quitting; irreversible
    bool seekTo(double seconds);                      // false while the engine has no stream
    bool seekFraction(double fraction);               // 0..1 of durationSeconds()
    void setShuffle(bool on);
    void setShuffleAlgorithm(ShuffleAlgorithm algorithm);
    ShuffleAlgorithm shuffleAlgorithm() const;
    void setRepeat(bool on);
    bool shuffle() const;                             // the user's choice
    bool shuffleActive() const;                       // shuffle && the queue is not endless
    bool repeat() const;
    const QueueRules& rules() const;                  // QueueRules{playReports, previousRestartsOnly}
    State state() const;                              // see Phases and State
    double positionSeconds() const;                   // the engine's position
    QUrl currentLink() const;                         // the current track's file; empty until it opens
    QUrl nextLink() const;                            // the preloaded file; empty without a preload

    const QList<Track>& playlist() const;
    const QString& queueTitle() const;
    int currentIndex() const;                         // -1 exactly when the queue is empty
    const Track* currentTrack() const;        // nullptr exactly when the queue is empty
    int currentBitrate() const;                       // kbps of the current file; 0 until it opens
    double durationSeconds() const;                   // Track::durationMs / 1000; 0 without a track
    int preloadedIndex() const;                       // -1 until the preload has an engine stream

    void setNetworkOnline(std::optional<bool> online); // the system's view; nullopt: cannot tell
    void setNetworkRetryDelays(int firstMs, int maxMs); // 2 s doubling to 60 s by default

Q_SIGNALS:
    void statusMessage(const QString& text);          // YandexSource::Catalog's, until #28
    void noticed(const Core::PlayerNotice& notice);   // see Notices
    void failed(Core::FailureKind kind);              // every failure the policy decides on
    void stateChanged(Core::Player::State state);     // once per pass of the event loop, on a change
    void playlistChanged();
    void queueReplaced();
    void currentTrackChanged();
    void positionTick();                              // every 100 ms while the engine isn't Stopped
    void modesChanged();                              // shuffle or repeat
    void seeked(double seconds);                      // the clamped target
};
```

### Threads and lifetime

- Everything runs on the thread that owns the `Player`, which is the GUI thread in the app. There
  are no locks. Link, download and load-more results arrive as Qt signals or callbacks on that
  thread. `TLoadMoreCallback` and `TEventCallback` are called on it, and a `TLoadMoreCallback` must
  call `done` on it too.
- The engine emits `trackFinished` and `trackAdvanced` only from `AudioEngine::poll()`. The only
  caller of `poll()` is `Player`'s own `QTimer` (100 ms interval). The timer runs whenever the
  engine state is not `Stopped` (it follows `AudioEngine::stateChanged`), so the end of a track
  is noticed with every window hidden or minimised. Each tick runs `poll()` first, together with
  any track change it triggers, then adds to the played seconds, then emits `positionTick`.
- `Player` does not own the `AudioEngine` or the `SourceRegistry`, and neither pointer is checked
  for null. Both, and the sources in the registry, must outlive the `Player`.

### The queue and the cursor

- `setQueue` and `appendTracks` keep only tracks with `Track::available == true`. Indices always
  refer to this filtered list.
- `currentIndex()` is a cursor. It is -1 exactly when the queue is empty. Otherwise it is a valid
  index, also while nothing plays. `setQueue` sets it to 0 and plays only with `autoplay`.
  `appendTracks` moves it from -1 to 0 and does not play.
- `removeTracks` takes indices of the queue before the removal. Duplicates and out-of-range indices
  are ignored. Removing tracks before the cursor shifts the cursor down. If the current track is
  removed, playback stops (it does not skip), and the cursor moves to the track that followed it,
  or to the new last track.
- `clearQueue()` takes a new ticket and calls `setQueue({}, {}, false)`. This also drops the
  `TLoadMoreCallback` and the `TEventCallback`.
- `insertTracks(index, tracks)` puts the available tracks at `index` (clamped to the queue). An
  index at or before the cursor shifts the cursor up by the number inserted; an empty queue gets
  the cursor 0. Then `playlistChanged` and `refreshPreload()`, which keeps the preload when its
  track still follows.
- `changeSource(title, more, events, rules)` keeps the queue, the cursor and the track that plays,
  and replaces what is behind them: the title, the `TLoadMoreCallback`, the `TEventCallback` (the
  open track still closes through the one it started with) and the `QueueRules`. It ends the wait
  for more, bumps `queueGeneration` (a late `done` of the old source is dropped), and emits
  `playlistChanged` and `modesChanged`, then `refreshPreload()`. `setQueue` resets the rules to
  the defaults.

### Phases and `State`

What the `Player` knows about the track that plays is one value: the track, whether its `Started`
went out, the callback and the `QueueRules::playReports` taken at that moment, its link and
bitrate, its feed (the engine stream, the download and how it ended, whether audio arrived), a
network wait and the played seconds. A stop drops it whole, so nothing of a stopped track lingers:
`currentLink()` is empty and `currentBitrate()` is 0 (`currentTrack()` is the cursor's and stays).

The current track's phase:

| Phase | `state()` | `Started` sent | Entered by | Left by |
|---|---|---|---|---|
| **Empty** | Empty | — | `setQueue` with no available track, `clearQueue`, removing every track | `setQueue` with tracks, `appendTracks` (to Stopped) |
| **Stopped** | Stopped | — | `stop`, `setQueue` without `autoplay`, the end of a finite queue, removing the current track, a failure the policy stops on (see [Failures](#failures-during-playback)), `playIndex` without an output device | `play`, `playIndex`, `next`, `previous` |
| **Opening** | Loading | no | `playIndex` without a usable preload: the engine stream is begun and the source asked | opened: **Open**. A `TrackFailure`: the [failure policy](#failures-during-playback). `stop`, `playIndex`, `setQueue`: the answer is dropped |
| **Open**, downloading | by the engine¹ | yes | the track opened and its download started, or the preload was taken over while still downloading | download OK: **Open, downloaded**. Download failed: the failure policy at once if no audio arrived, else **Open, failed**. End or skip: see [Inputs](#inputs-in-the-order-they-act) |
| **Open**, downloaded | by the engine¹ | yes | the current download finished without error, or a finished preload was taken over | end or skip. Only in this phase can a new preload start |
| **Open**, failed | by the engine¹ | yes | the current download failed after some audio arrived; the engine plays what arrived | end: a network failure **Waiting for the network**, any other failure the end reported as `Skipped`. Skip |
| **Waiting for the network** | WaitingForNetwork | as before | a network failure (ERR-01 to ERR-03) | the track opens and its download starts: **Open** again, playing if it played before. Another failure: the policy. `stop`, `playIndex`, `setQueue`: the wait ends |
| **Waiting for more** | WaitingForMore | — | `next()` at the end of an endless queue | `done` with new tracks: `playIndex(cursor + 1)`. `done({})`: still waiting. `play`, `playIndex`, `stop` or `setQueue` end the wait |
| **Shut down** | Stopped or Empty | — | `shutDown()` | never |

¹ The engine's Playing is Playing, its Paused is Paused, Buffering (or Stopped in the moment
before the `Player` handles the end) is Loading.

`state()` is computed on every call, never stored: a current track that waits for the network is
WaitingForNetwork, any other current track follows the engine as above. Without a current track:
WaitingForMore while waiting for more; an engine that plays anyway follows the engine (only
`--play-file`, which feeds the engine past the `Player`); otherwise Empty for an empty queue and
Stopped for any other.

`stateChanged(state)` comes at most once per pass of the event loop, after the pass, and only when
`state()` differs from the state it last carried. The engine reports its states synchronously in
the middle of the `Player`'s transitions, and none of those leaks out: Stop gives `[Stopped]`, a
last track that cannot play (ERR-07) `[Stopped]`, a lost network `[WaitingForNetwork]` (both when
the track fails to open and when its audio runs out, ERR-03), Play without an output device
`[Stopped]` (`player_test`).

What each state looks like:

| `State` | Main and playlist windows | `MediaControls` | `JamMode`'s report |
|---|---|---|---|
| Empty, Stopped, WaitingForMore | stopped | Stopped | WaitingForMore: idle; else the track at 0, paused |
| Loading | playing, the red light | Playing | paused |
| Playing | playing, the green light | Playing | playing |
| Paused, WaitingForNetwork | paused, the time blinking | Paused | paused, the position kept |

The preload's phase:

| Phase | `preloadedIndex()` | Meaning |
|---|---|---|
| none | -1 | |
| opening | -1 | the source was asked for the next track |
| queued | its index | `AudioEngine::queueStream()` gave it a stream. Its download runs, or has finished |

The load-more request: idle, or in flight. It is independent of both phases above.

### Inputs, in the order they act

- **`setQueue(tracks, title, autoplay, more, events)`**
  1. `stop()`: the open track gets `Skipped` through the callback it started with, and the
     current track (its open answer, download and wait) and the preload are dropped.
  2. The queue becomes the available tracks, and the title, `TLoadMoreCallback` and `TEventCallback`
     are replaced. The load-more and waiting flags are reset, and `queueGeneration` is bumped.
  3. The cursor goes to 0, or to -1 when the queue is empty.
  4. `queueReplaced`, then `playlistChanged`, then `currentTrackChanged`.
  5. If `autoplay` is set and the queue is not empty: `playIndex(0)`.
- **`playIndex(i)`** does nothing after `shutDown()` or when `i` is out of range. Otherwise:
  1. The wait for more ends and the current track is closed: `Skipped` if its `Started` went out.
     Its open answer, download and network wait go with it. The cursor moves to `i`.
  2. **The preload is taken over** when it has an engine stream, its key equals the track's, and
     that stream is still `AudioEngine::queuedStream()`. `playQueuedNow()` starts it from its
     beginning at once, reusing the bytes already downloaded, and the preload's feed, link and
     bitrate become the current track's. Then `currentTrackChanged`, the load-more check, and the
     start.
  3. **Otherwise:** `cancelPreload()`, then `AudioEngine::beginStream()`. Without an output device
     no track opens: `currentTrackChanged`, the load-more check and the notice `NoOutputDevice`,
     and the state stays Stopped. Otherwise
     `currentTrackChanged` (with `currentBitrate()` still 0), the load-more check, then
     `SourceRegistry::open` (so do the network retry and the preload). An answer that comes after
     `stop`, `playIndex` or `setQueue` is dropped, and its download never starts. A `TrackFailure`:
     the failure policy. Opened: the download is connected to the stream begun above and started,
     then the start.
- **The start** (private): stores the bitrate, takes the current `TEventCallback` and
  `QueueRules::playReports` for all of the track's events, resets the played seconds, sends
  `Started(track, 0)` to the source (with `playReports`) and then to the callback, emits
  `currentTrackChanged` and calls `maybePreload()`.
- **`next()`**: with an empty queue, nothing happens. If a preload exists in any phase:
  `playIndex(preload index)`. Else if `pickNext() >= 0`: `playIndex` of it. Else, if the source is
  endless: `stop()`, mark waiting, run the load-more check, then emit
  the notice `LoadingMore`. Else (the end of a finite queue with repeat off):
  `stop()`, and the cursor stays on the last track.
- **`previous()`** (spec TR-01, TR-02): more than
  `kPreviousRestartsAfterSeconds` (3 s) into a playing or paused track, it restarts that track
  with `seekTo(0)`. That is a seek, not a new start: no events and no play report; if the engine
  refuses the seek, `playIndex(cursor)`. Earlier, or while stopped: `playIndex(cursor - 1)`; at
  position 0 the last track if repeat is on, the first otherwise. With shuffle, the cursor
  and target follow the navigator: stored order for WithoutRepeats, source order for Random.
- **`play()`**, by engine state: Paused resumes. Playing calls `playIndex(cursor)`, which restarts
  the track as Winamp does, with `Skipped` and then a new `Started`. Buffering does nothing.
  Stopped calls `playIndex(cursor)`, or `playIndex(0)` when the cursor is -1.
- **`pause()`**: Paused resumes. Playing or Buffering pauses. Stopped does nothing.
- **`stop()`**: sends `Skipped` for the open track, drops the current track whole, ends the wait
  for more, cancels the preload and stops the engine. It keeps the queue, the cursor and the
  `TLoadMoreCallback`. Of its own signals only `stateChanged` follows, after the pass; the engine
  emits `stateChanged(Stopped)`, and that stops the timer.
- **Engine `trackFinished`**: when the download broke on the network after audio arrived, the track
  stays open and waits for the network (ERR-03). Otherwise it gets `Finished`, or `Skipped` if its
  download failed, with the played seconds. Then, if repeat is on and the queue has exactly one
  track, `playIndex(cursor)`; otherwise `next()`.
- **Engine `trackAdvanced`** (the engine crossed into the queued stream with no gap): the open track
  gets `Finished` or `Skipped` as above. If there is no preload, or its stream is not the engine's
  `currentStream()`, or its index is out of range, the player resyncs:
  `playIndex(pickNext())`, or `stop()` when that is -1. A cancelled preload is always cleared
  from the engine, so this branch is not expected to run. Otherwise the preload is taken over as in
  `playIndex`, without `playQueuedNow()`, at the preload's own index.
- **Download finished**:
  - The current track's, failed: when no audio arrived on its stream, the failure policy at once;
    otherwise the failure kind is kept for the track's end (a network failure then waits, ERR-03;
    any other gives the notice `DownloadFailed` and ends the track as `Skipped`).
    A preload taken over brings what arrived on its own stream.
  - The current track's, succeeded: then `maybePreload()`.
  - The preload's: a failure cancels the preload (that track is fetched again when its turn
    comes).
- **`appendTracks`**: appends the available tracks. If none were added, it returns without a
  signal. Otherwise the cursor moves from -1 to 0 if needed, then `playlistChanged`, then
  `refreshPreload()`.
- **`removeTracks`**: see [the queue](#the-queue-and-the-cursor). If the current track was
  removed: `stop()`, then `currentTrackChanged`. Then, always, `playlistChanged` and
  `refreshPreload()`.
- **`SourceRegistry::filesChanged(source)`** (a source's quality changed): if the preload is
  another source's track, nothing. Otherwise `cancelPreload()`, then `maybePreload()`, so a next
  track fetched in the old quality is fetched again (spec QUAL-05). The current track goes on with
  its file; no signal is emitted.
- **`setShuffle`, `setRepeat`**: nothing happens without a change. With a change:
  `modesChanged`, then `refreshPreload()`. Switching shuffle on while the queue is endless also
  gives the notice `ShuffleOffInVibe`; `setQueue` gives the same when an
  endless queue starts with shuffle on.
- **`seekTo(seconds)`**: clamps the target to `[0, durationSeconds()]`, or to `>= 0` when the
  duration is 0. It returns `false` and emits nothing when `AudioEngine::seek` refuses, which
  happens while there is no stream (stopped, or no audio device). On success it moves
  the played-seconds baseline to the target and emits `seeked(target)`. `seekFraction(f)` is
  `seekTo(clamp(f, 0, 1) * durationSeconds())`, and returns `false` when the duration is 0.
- **`shutDown()`**: takes a new ticket, so source loads still in flight become stale. Then
  `stop()`, which reports the open track as `Skipped`; for a wave that becomes the "skip"
  feedback. From then on `playIndex` and `maybePreload` do nothing.

### Tickets and generation counters

Four counters, each bumped by different calls and each guarding one kind of late reply:

| Counter | Bumped by | Drops |
|---|---|---|
| `sourceRequest` (the ticket) | `newSourceRequest()`: callers before a source load, `clearQueue`, `shutDown` | a caller's source-load reply, when the caller checks `isLatestSourceRequest(ticket)`. `Player` never checks it itself |
| `queueGeneration` | `setQueue` (and so `clearQueue`) | a `TLoadMoreCallback`'s `done` for a queue that has been replaced (a `QPointer` also covers a destroyed `Player`) |
| `generation` | closing the current track (`stop`, `playIndex`, a track's end), opening it, the network wait, a preload taken over | the open answer of a track that is no longer current |
| `preloadGeneration` | every new preload | the open answer of a preload that was cancelled or replaced |

Tickets start at 1, so 0 never matches. `YandexSource::Catalog` takes one for every pick. The
next step of [#23](https://github.com/Kickoman/QiYaa/issues/23) makes the phases a
`std::variant`, each answer bound to the phase that asked; the counters then go.

### Endless sources (`TLoadMoreCallback`)

- A queue is endless when `setQueue` got a `TLoadMoreCallback`. At its end `pickNext()`
  returns -1 even with repeat on, so it never wraps. `next()` waits for more instead.
- The load-more check (`maybeLoadMore`) calls the `TLoadMoreCallback` when all of these hold: the
  queue has one, no request is in flight for it, and `playlist().size() - currentIndex() <=
  kLoadMoreWhenLeft` (2). That means the current track is the last or the second to last.
- The check runs when a track becomes current (`playIndex` in both branches, before the link is
  resolved, and the gapless advance) and when `next()` reaches the end. It does not run from
  `setQueue` without `autoplay`, `appendTracks`, `removeTracks` or a mode change.
- `done(tracks)` is ignored if the `Player` is gone or the queue has been replaced. Otherwise the
  request is no longer in flight and `appendTracks(tracks)` runs. If `next()` was waiting and a
  track now follows the cursor, `playIndex(cursor + 1)` runs, the first new track; if nothing came,
  the player keeps waiting (`State::WaitingForMore`), and a later `next()`, or tracks inserted by
  the jam, go on from there.
- `requestMore()` runs the check from outside, for a source that learns of new material between
  track changes (the jam's new seeds).
- `done` may be called synchronously from inside the `TLoadMoreCallback`.

### Next, shuffle, repeat

- `pickNext()` delegates to `TrackNavigator::next`, passing Repeat only for finite queues.
  Previous uses the navigator too, unless it must restart the current track after 3 s.
- Shuffle does not apply to an endless queue (spec WAVE-10, WAVE-11): a wave always uses sequential
  traversal. The shuffle button and preferred algorithm retain the user's choices for finite queues.
- `shuffleAlgorithm()` defaults to WithoutRepeats. The application restores and saves it. Switching
  algorithms while shuffle is active rebuilds navigation and refreshes preload; while inactive it
  changes only the preference. Audio, position and pause state are preserved in either case.
- A new queue, a source change, edits (append, insert, remove), or switching shuffle resets the
  navigator from the current track. Queue edits start a new shuffled pass, which can include
  tracks heard before the edit. A direct `playIndex` selects the entry in the existing order.
- `next()` and preload read the same next entry. Looking ahead never consumes it. Explicit Next
  and gapless advance call `select` when a track becomes current.
- Repeat with a single track replays it, including through gapless preload. An endless queue with
  one track does not replay it: it waits for more (spec WAVE-12).

### Downloads

- The file comes from the `TrackDownload` the source opened. The `Player` connects it to its
  stream before it calls `start()`.
- Every `received` passes the bytes to `AudioEngine::appendData(stream, …)`, with the stream id
  captured at the start. `finished` calls `finishData(stream)`, or `failData(stream)` with a
  `TrackFailure`. The engine ignores data for streams it has dropped.
- `Player` sets no byte limit. Every byte goes to the engine, which keeps the stream in memory.
- At most one track download is in flight. A preload is asked for only after the current track's
  download has finished without error. A dropped download is deleted, which aborts it without
  signals.
- A finished download is taken out of the `Player` and deleted later, before the `Player` acts on
  its result, so it is never deleted inside its own signal.

### Failures during playback

The rules are [spec/player/errors.md](../../spec/player/errors.md); `tests/failures_test.cpp`
checks them by ID. `failure_policy.h` holds the decision as pure functions, the counterpart of
the Android app's `ErrorPolicy`:

```cpp
enum class FailureKind { Network, Auth, Track };
enum class FailureAction { WaitForNetwork, Next, Stop, StopAfterLimit };
inline constexpr int kMaxTrackFailuresInRow = 3;
FailureAction DecideOnFailure(FailureKind kind, int failuresInRow, bool hasNext, bool endless);
```

- **Where a failure comes from:** opening the track and its download (the source's
  `TrackFailure`; Yandex's kinds are `YandexSource::FailureKindOf`), and
  `AudioEngine::streamUndecodable` (a Track failure, or a Network one if the download had already
  broken off).
- **Track** (ERR-04, ERR-05, ERR-07): the notice `TrackFailed` (with `undecodable` when the sound
  could not be decoded) and `next()`, which in a wave at its end waits for more. The third in a row
  stops instead, with `StoppedAfterFailures` (count 3); with no next track in a finite queue it
  stops at once. The count goes back to 0 when the engine reaches Playing, and in `setQueue`
  (ERR-06).
- **Auth** (ERR-08): the notice `AccessDenied` and a stop. It is not counted.
- **Network** (ERR-01 to ERR-03): the track is parked. The notice `WaitingForNetwork` once; the
  download and the preload are dropped, and a new stream is begun at the position the track had
  reached and paused, so every view shows it paused there. The track stays open: no `Skipped`
  now, and no new `Started` or play report when it continues. A retry follows after 2 s, then
  4 s, 8 s … up to 60 s (`setNetworkRetryDelays`); `setNetworkOnline(true)` retries at once,
  `setNetworkOnline(false)` holds the retries until then. A retry opens the track again and
  streams into the parked stream, which seeks to the kept position before any audio; it plays on
  if the track was playing and the user has not paused since (Play and Pause during the wait only
  set that). `stop`, `playIndex` and `setQueue` end the wait.
- `src/app` feeds `setNetworkOnline` from `QNetworkInformation` when a backend can tell
  reachability; without one the timer alone ends a wait.

### Gapless preload

- **Started** by `maybePreload()` when all of these hold: there is no preload; the player is not
  shut down; a track is open and its download has finished without error; the engine is not
  `Stopped` (Paused counts); and `pickNext() >= 0`. It is called at the end of the start, when the
  current download finishes, and from `refreshPreload()`.
- **Open answer** (ignored if the `Player` is gone or `preloadGeneration` has moved on): the
  preload asks the engine for a queued stream. Its download then feeds that stream and
  `preloadedIndex()` becomes its index. On a `TrackFailure`, or when `queueStream()` returns 0
  because the engine has stopped on its own meanwhile, the preload is dropped without a message.
  That track then starts the ordinary way when its turn comes.
- **Taken over** in one of two ways:
  1. The engine crosses the boundary (`trackAdvanced`), with no gap and no second open.
  2. `playIndex` of a track with the preload's key while its stream is still the engine's queued
     stream. `next()` always takes this path when the preload is queued.

  In both cases a download that is still running continues as the current download, and a failed
  one has already cancelled the preload.
- **Cancelled** by `cancelPreload()`, in `stop()`, in every `playIndex` that does not take it over,
  and in `refreshPreload()`. Cancelling drops its download, which aborts it, and calls
  `AudioEngine::clearQueued()` if its stream is still the queued one. The engine may already have
  committed to chaining it (the last seconds of the current track; see
  [src/audio](../audio/README.md)). In that case playback halts at the boundary and `trackFinished`
  follows, so `next()` picks again: the result is a gap, never the wrong track.
- **Refreshed** by `refreshPreload()` after `appendTracks`, `removeTracks`, and any actual change of
  shuffle or repeat. The preload is kept, and its index updated, when its track id is still what
  follows at `pickNext()`, in either mode. Otherwise the preload is cancelled.
  Whenever no preload is kept, `maybePreload()` tries a new one.
- A queued stream that the engine cannot decode is forgotten by the engine, and
  `queuedStream()` returns 0 for it. The next `playIndex` of that track then fetches it again the
  ordinary way.

### Track events and play reports

- Every track that opens gets `Started(track, 0)`, followed by exactly one `Finished` or
  `Skipped` with its played seconds. The closing event goes to the `TEventCallback` that received
  `Started`. It is always sent before the `Started` of the next track, because `playIndex` and
  both engine handlers close the open track first.
- `Started` is sent:
  - after the track opens and its download starts, before any audio of it is heard;
  - at once, when the preload is taken over by `playIndex` or `next()`;
  - at the gapless boundary.
- `Finished` is sent when the engine reports the track's end (`trackFinished` or `trackAdvanced`)
  and its download did not fail.
- `Skipped` is sent in every other case: `stop`; `playIndex`, `next` or `previous`; `play` while
  playing; `setQueue` or `clearQueue`; removing the track; `shutDown`; or an end after a failed
  download, where the track only drained to where the data stopped.
- No event is sent for a track that failed to open, because it never opened; nor without an output
  device, where no track opens. None is sent when the `Player` is destroyed with a track open
  either; call `shutDown()` first, as `App::Application::quit` does. An open answer that arrives
  after the `Player` is gone is dropped (every answer holds a `QPointer` to it).
- The played seconds are the seconds actually heard. On every tick and at close, the position
  step since the previous reading is added if the engine is `Playing` and `0 < step < 1.0`.
  Normal steps are about 0.1 s. Seeks and other jumps of 1 s or more, backward moves, and paused
  or buffering time do not count. `seekTo` also moves the baseline to the target.
- Each event also goes to the track's source (`SourceRegistry::trackEvent`) before the
  `TEventCallback`, whether or not the queue has one — unless `QueueRules::playReports` was false
  when the track started (the jam, HOST-15). For a Yandex track `Started` is the play report.
- With `QueueRules::previousRestartsOnly` (the jam, HOST-07), `previous()` always restarts the
  current track by `seekTo(0)`, or `playIndex(cursor)` when the engine refuses the seek.

### Signals

| Signal | Emitted |
|---|---|
| `statusMessage(text)` | never by the `Player` itself: `YandexSource::Catalog` emits it on the `Player` for picks, likes and dislikes until [#28](https://github.com/Kickoman/QiYaa/issues/28) |
| `noticed(notice)` | see [Notices](#notices) |
| `stateChanged(state)` | after a pass of the event loop in which `state()` changed; see [Phases and `State`](#phases-and-state) |
| `playlistChanged` | by `setQueue`; by `appendTracks` when something was added; by `removeTracks` always, even when no index was valid |
| `queueReplaced` | by `setQueue` and `clearQueue` only, before `playlistChanged` |
| `currentTrackChanged` | by `setQueue`, even for an empty queue; by `playIndex` at once, also without an output device; again at the start, once the bitrate is known; twice on a gapless advance; by `removeTracks` when the current track was removed |
| `positionTick` | after each timer tick's `poll()`, every 100 ms while the engine is Buffering, Playing or Paused |
| `modesChanged` | by `setShuffle`, `setShuffleAlgorithm` and `setRepeat` on a change |
| `seeked(target)` | after a successful `seekTo` or `seekFraction`, with the clamped target |

### Notices

```cpp
enum class NoticeCode { ShuffleOffInVibe, AudioError, NoOutputDevice, LoadingMore, DownloadFailed,
                        TrackFailed, AccessDenied, StoppedAfterFailures, WaitingForNetwork };
struct PlayerNotice {
    NoticeCode code;
    QString detail;           // English, for AudioError, DownloadFailed, TrackFailed, AccessDenied
    int count = 0;            // StoppedAfterFailures: how many in a row
    bool undecodable = false; // TrackFailed: the sound could not be decoded; detail is empty
};
```

What the `Player` has to tell the user goes out as `noticed`, never as text: `Ui::NoticeText`
([src/ui](../ui/README.md)) words it in the user's language, and the main window shows it in the
marquee. `detail` is not translated: it is the source's or the engine's own English text (an HTTP
status and path, an engine message). Codes for the engine's errors are a later step.

| Code | When |
|---|---|
| `ShuffleOffInVibe` | shuffle switched on in an endless queue, or an endless queue set with shuffle on (WAVE-10) |
| `AudioError` | `AudioEngine::errorOccurred` |
| `NoOutputDevice` | `playIndex` without an output device |
| `LoadingMore` | `next()` at the end of an endless queue |
| `DownloadFailed` | the current download failed after some audio arrived, not on the network |
| `TrackFailed` | the policy skips the track or stops on it (ERR-04, ERR-07) |
| `AccessDenied` | an Auth failure (ERR-08) |
| `StoppedAfterFailures` | the third track in a row did not play (ERR-05) |
| `WaitingForNetwork` | a network wait begins (ERR-01 to ERR-03) |

**Traps:**

- `pause()` toggles, and `play()` restarts the track while it is playing. Code that serves media
  keys has to check the state first, as `src/integrations` does.
- `setQueue` ignores tickets. An asynchronous source load must take `newSourceRequest()` before
  the request and drop the reply unless `isLatestSourceRequest(ticket)`. A direct `setQueue` does
  not invalidate loads in flight; only a new ticket does. That is why `clearQueue` and `shutDown`
  call `newSourceRequest()` and throw the ticket away. Do not remove those calls.
- A `TLoadMoreCallback` must call `done` exactly once, on the `Player`'s thread, with an empty list
  on failure. Until it does, that queue never asks again. After `done({})`, a waiting `next()` stays
  waiting after its `LoadingMore`; the next `next()` asks again. If `done` runs synchronously
  inside the `TLoadMoreCallback`, playback has already moved on when `next()` gives that notice.
- `TLoadMoreCallback` and `TEventCallback` run synchronously inside `Player` methods and the timer's
  handler. They must not call `playIndex`, `stop` or `setQueue`, and they must not throw (see
  [Errors](#errors)).
- A source may answer `open` after the `Player` is gone: every reply checks a `QPointer` to it.
- The poll timer calls `accumulatePlayedSeconds()` for its side effect; dropping the call loses
  the played time. A stall of the GUI thread longer than 1 s between two ticks also loses that
  stretch.
- `durationSeconds()` is `static_cast<double>(durationMs) / 1000.0`, using the metadata duration,
  not the decoded length. `seekTo` clamps to it.
- Removing a track before the current one changes `currentIndex()` without
  `currentTrackChanged`. The same is true when `appendTracks` moves the cursor from -1 to 0. UI
  that depends on the index must also listen to `playlistChanged`.
- Without an output device no track opens: Play only moves the cursor and gives
  `NoOutputDevice`. Nothing reaches the source or the callback, and the load-more check still runs.
- `shutDown()` blocks playback only. `setQueue`, `appendTracks` and `removeTracks` still change
  the list, and `next()` at the end of an endless queue still calls the `TLoadMoreCallback`.
- `Started` means "the download began", not "audio is audible".
- `stateChanged` comes a pass after the change. Code that acts right after a call reads `state()`,
  which is current at once; a listener gets one signal per settled state, not one per engine
  transition.
- The timer starts on the engine's next `stateChanged`. An engine that is already playing when
  the `Player` is constructed is not polled until its state changes.

## `track.h`: `Track`, `TrackKey`, `CoverRef`

```cpp
enum class TrackEvent { Started, Finished, Skipped };
struct TrackKey { QString source; QString id; bool operator==(const TrackKey&) const = default; };
struct CoverRef { QString value; };
struct Track {
    TrackKey key;
    QString title;
    QStringList artists;
    QString album;
    qint64 durationMs = 0;
    int year = 0;
    QString genre;
    CoverRef cover;
    bool available = true;
    QString displayTitle() const;   // "Artist1, Artist2 - Title", or the title alone
};
```

- A track of any music source ([src/sources](../sources/README.md)): the queue, the current track,
  the callbacks and the jam all hold these. `key.source` names the source (`yandex`); `key.id` is
  the source's own and means nothing to the core. A Yandex track's is `<track id>:<album id>`, or
  the track id alone.
- `TrackEvent` (`Started`, `Finished`, `Skipped`) is what happened to a track in the queue; the
  `Player` reports it to the track's source and to the queue's callback.
- `cover` is read only by the track's source, through `TrackSource::coverUrl`.
- What only one source needs is not here: the source encodes it in `key.id` or keeps it.

## `track_source.h`: `TrackSource`, `SourceRegistry`

```cpp
struct TrackFailure { FailureKind kind = FailureKind::Track; QString text; };   // text: English, for the status line

class TrackDownload : public QObject {
public:
    virtual void start() = 0;                     // sends the request; once
Q_SIGNALS:
    void received(const QByteArray& bytes);       // the file's next bytes, in order
    void finished(const std::optional<Core::TrackFailure>& failure);   // once, last
};

struct OpenedTrack {
    std::unique_ptr<TrackDownload> download;      // not started yet
    QUrl link;                                    // what a jam's guests may fetch (about an hour)
    int bitrateKbps = 0;
    QString codec;
};
using TOpenResult = std::variant<OpenedTrack, TrackFailure>;

class TrackSource : public QObject {
public:
    using TOpened = std::function<void(TOpenResult result)>;
    virtual QUrl coverUrl(const Track& track, int pixels) const = 0;   // empty: no cover
    virtual QUrl webPage(const Track& track) const = 0;                // empty: no web page
    virtual void open(const Track& track, TOpened done) = 0;
    virtual void trackEvent(TrackEvent event, const Track& track, double playedSeconds) = 0;
Q_SIGNALS:
    void filesChanged();                          // the same track would now open another file
};

class SourceRegistry : public QObject {           // built by src/app
public:
    void add(const QString& source, TrackSource* port);                // does not take ownership
    TrackSource* find(const QString& source) const;                    // nullptr: unknown source
    QUrl coverUrl(const Track& track, int pixels) const;               // the track's source's; empty if unknown
    QUrl webPage(const Track& track) const;
    void open(const Track& track, TrackSource::TOpened done);
    void trackEvent(TrackEvent event, const Track& track, double playedSeconds);
Q_SIGNALS:
    void filesChanged(const QString& source);
};
```

- A port answers for its own source's tracks. `coverUrl` and `webPage` are cheap and pure: no
  input or output, and the same URL for the same track and size; `coverUrl` runs on every
  `CoverCache::ready`. For another source's track they return empty URLs.
- `open` calls `done` exactly once, from the event loop and never inside `open` itself. A port
  that is destroyed first may drop it. The download in an `OpenedTrack` sends nothing until
  `start()`, so a reply that comes too late is dropped at no cost.
- A `TrackDownload` emits nothing before `start()` and never inside it. `finished` comes once and
  last; a `TrackFailure` there is the download's (a cut connection is `Network`). Deleting a
  download aborts it, and nothing is emitted after that. The `Player` never deletes one inside its
  own signal.
- `trackEvent` takes every event of a track that started with `QueueRules::playReports`; what the
  source does with it is its own (Yandex reports `Started` only).
- `filesChanged`: the source now opens other files for the same tracks (another quality). The
  registry re-emits it with the source's name.
- The registry routes by `track.key.source`. A track of a source it does not know fails to open
  with `TrackFailure{Track}` on the next pass of the event loop, and its events go nowhere.
- Like, dislike and the other capabilities of a source are not in the port yet: the Yandex menu
  asks `YandexSource::Catalog`.

## `continuation.h`: `Continuation`

```cpp
class Continuation {
public:
    using TDone = std::function<void(const QList<Track>& tracks)>;
    virtual void more(TDone done) = 0;                                                       // as TLoadMoreCallback
    virtual void trackEvent(TrackEvent event, const Track& track, double playedSeconds) = 0;   // as TEventCallback
};
```

What a queue goes on with after its fixed tracks, in the shape of the `Player`'s two callbacks:
`more` calls `done` once, later, with the next tracks (empty when there are none), and
`trackEvent` hears the events of the tracks it gave. Today only the jam's wave is one
(`JamWave`); [#28](https://github.com/Kickoman/QiYaa/issues/28) makes it every queue's
continuation.

## `track_navigator.h`: `TrackNavigator`

```cpp
enum class ShuffleAlgorithm { Random, WithoutRepeats };
class TrackNavigator {
    virtual void reset(int size, int current) = 0;
    virtual void select(int index) = 0;
    virtual int next(bool repeat) const = 0;
    virtual int previous(bool repeat) const = 0;
};
std::unique_ptr<TrackNavigator> MakeTrackNavigator(std::optional<ShuffleAlgorithm> algorithm);
```

No algorithm means sequential traversal. The three implementations are private to the `.cpp`;
Player knows only the interface. Navigation is independent of audio, networking, settings and UI.
`reset` takes a queue size and its valid current index (or -1 for an empty queue). `select` takes
an existing queue index. `next` and `previous` return queue indices, or -1 when no entry follows.
Repeated lookahead returns the same candidate and does not advance. Empty queues return -1.

- `SequentialNavigator`: follows source order. Repeat wraps at boundaries; Previous without
  Repeat on the first entry returns that entry. O(1) space and operations.
- `RandomNavigator`: chooses uniformly from all indices except the current one on `select` or
  `reset`. The candidate is cached for lookahead/preload. Repeats across transitions are allowed;
  with at least two entries playback continues even with Repeat off, matching the old random
  behaviour. Previous follows source order. O(1) space and operations.
- `ShuffledNavigator`: keeps a permutation, with the current entry first and the rest shuffled
  via `std::shuffle` and `QRandomGenerator::global()`. Each entry appears once per forward pass;
  duplicate track ids remain separate entries. Repeat wraps through the same permutation and
  Previous follows it. Storage is one `int` per entry; reset and select are O(n), lookahead O(1).
  The order is session-only. The displayed playlist stays in source order.

Tests cover the interface without an audio engine, plus Player's real preload and gapless transitions.

## `jam_mode.h`: `JamMode`

```cpp
struct JamEntry { QString itemId; Track track; QString addedBy; };
struct JamSlot { enum class Kind { Item, Wave, Other }; Kind kind; QString itemId; QString addedBy; };
struct JamPlayback { enum class Kind { Item, Wave, Idle }; Kind kind; QString itemId;
                     std::optional<Track> track; qint64 positionMs; bool paused; QUrl link, nextLink; };
inline constexpr int kJamPlayingReportMs = 10'000;
inline constexpr int kJamWaveHistory = 5;

class JamWave : public Continuation {                // one jam's wave; a source makes it
public:
    using TRecent = std::function<QList<Track>()>;
    virtual bool start(const QStringList& seeds, int seedsVersion, const Track* current, TDone done) = 0;
    virtual bool hasSession() const = 0;
    virtual int seedsVersion() const = 0;
    virtual void endSession() = 0;
};
using TJamWaveFactory = std::function<std::shared_ptr<JamWave>(bool feedback, JamWave::TRecent recent)>;

class JamMode : public QObject {
public:
    JamMode(Player* player, TJamWaveFactory makeWave, QObject* parent = nullptr);
    bool isActive() const;
    const QList<JamSlot>& queueSlots() const;         // one per track of Player::playlist()
    JamSlot currentSlot() const;
    void start(const QString& title, bool waveFeedback);
    void setQueue(const QList<JamEntry>& entries, const QStringList& seeds, int seedsVersion);  // each state
    void skip(const QString& itemId);                 // command{skip}
    void end();
    void reportPlayback();                            // now, as after `resumed` (HOST-26)
    void setPlayingReportInterval(int ms);            // for tests
Q_SIGNALS:
    void itemStarted(const QString& itemId);          // -> started{itemId}
    void playback(const Core::JamPlayback& playback); // -> playing
};
```

The scenarios are [spec/jam/host.md](../../spec/jam/host.md) with Desktop = Kickoman/QiYaa#13;
`tests/jam_mode_test.cpp` checks them by ID. The network, the protocol and the server's messages are
the host session's (Kickoman/QiYaa#14); `JamMode` gets the queue as `JamEntry`s and reports through
its two signals.

- **The queue** is the current track, then the **jam part** (the mirror of the server's queue), then
  the **jam wave**. `queueSlots()` says which each track of `Player::playlist()` is: `Item` (with
  its item id and who added it), `Wave`, or `Other` (the track that played when the jam started).
  Every change of the queue during the jam goes through `JamMode`, which changes its slots first
  and the `Player` second; a size that differs afterwards (someone else edited the queue) is
  logged and the slots are cut or padded.
- **Start** (HOST-14): every track but the current one is removed, and `Player::changeSource`
  makes the queue endless with `QueueRules{playReports = false, previousRestartsOnly = true}`
  (HOST-15, HOST-07). The track that plays goes on and is reported as a wave track. An endless
  queue already turns shuffle off (WAVE-10) and repeat does not wrap or replay it (WAVE-12), so the
  user's modes need no change.
- **The jam part** (HOST-01 to HOST-04): `setQueue` keeps the longest common prefix of the jam part
  and the server's queue, removes the rest of the jam part and inserts what differs, so a next
  track that did not change keeps its preload. The current item is left out even while the state
  still lists it. Into an empty queue, the first item plays at once; a new item after the cursor
  plays at once when the player waits for more, or is Stopped or Empty with nothing after the
  cursor (HOST-08).
- **The jam wave** (HOST-10 to HOST-13) is the `Player`'s load-more. `start` makes one `JamWave` for
  the jam with the factory (`feedback` = `waveFeedback`; `recent` gives the last 5 wave tracks of
  the queue, `kJamWaveHistory`). When at most 2 tracks are left, `JamMode` asks `JamWave::more`
  while the wave has a session started under the last state's `seedsVersion`, and otherwise
  `JamWave::start` with the last state's seeds and the current track. `start` returning false (no
  seeds and nothing played) means no wave and `playing{idle}`. When the jam part becomes empty again
  under new seeds (the current track is not a wave track, no item follows), the old session's
  deferred tracks are removed, `endSession()` is called, and the next load starts a new session.
  What the wave asks of its service:
  [src/sources/yandex](../sources/yandex/README.md#jam_waveh-jamwave-jamwaves).
- **What `JamMode` expects of a `JamWave`**: `start` and `more` call `done` once, later, with the
  tracks (empty when there are none); `start` returns false, without calling `done`, when it has
  nothing to start from. `trackEvent` is called for every event of a wave slot's track, and after
  the jam ended for any track; the wave ignores tracks it did not give.
- **Late replies.** A reply for the wave of an earlier jam (one that ended, with a new one started
  before the reply came) is dropped. So is a `more` reply for a session that new seeds closed while
  it was asked for (HOST-11): `JamMode` then asks again at once, which starts the new session.
- **Not learning** (HOST-15, HOST-16): no play reports; rotor feedback (radioStarted, trackStarted,
  trackFinished, skip) only for the jam wave's tracks, to the session each came from, and only when
  `start` got `waveFeedback`.
- **Listening along** (`spec/jam/listen.md`). `JamPlayback::link` is `Player::currentLink()`: the
  file of the current track once its link resolved, including a preloaded track that took over;
  `JamPlayback::nextLink` is `Player::nextLink()`, the preloaded next file. The host session sends
  them to the jam's guests when the host allows it.
- **Reports**: `itemStarted(itemId)` when a jam item's `Started` fires; `playback` after every
  track event, on the `Player`'s `stateChanged`, seeks, and every 10 s while `state()` is Playing
  (HOST-05, HOST-06). `Idle` when nothing plays (empty queue, or waiting at the end); the rest by
  the table in [Phases and `State`](#phases-and-state).
- **Skip** (HOST-18, HOST-19): `skip(itemId)` is `Player::next()` only when that item is current.
- **End** (HOST-32, HOST-33): the wave tracks after the cursor are removed, the jam items stay as
  ordinary tracks, the current track plays on, and `changeSource` with the default rules brings the
  play reports and a finite queue back.
- `YandexSource::Catalog::setJamMode(jam)` refuses every pick while the jam is on (HOST-21).

**Traps:**
- The `Player`'s load-more and event callbacks hold a `QPointer<JamMode>` and the jam's
  `std::shared_ptr<JamWave>`: a wave track that closes after `end()`, or while the app shuts down,
  still gets its feedback.
- HOST-09 (a broken last jam track waits for the jam wave) is the `Player`'s own ERR-07 for an
  endless queue: `DecideOnFailure` says Next (the policy table in `tests/failures_test.cpp`), and
  `next()` at the end waits for more. `jam_mode_test` does not repeat it.

## `cover_cache.h`: `CoverCache`

```cpp
class CoverCache : public QObject {
public:
    // cacheDirectory empty = QStandardPaths::writableLocation(CacheLocation) + "/covers"
    explicit CoverCache(
        QNetworkAccessManager* networkAccessManager,
        const QString& cacheDirectory = {},
        QObject* parent = nullptr
    );

    QImage get(const QUrl& url);               // null while it downloads; ready(url) follows on success
    QString localFile(const QUrl& url) const;  // empty until the file is on disk

Q_SIGNALS:
    void ready(const QUrl& url);
};
```

- The app has one instance, shared by the "Now playing" window and the system media integration.
  The integration needs a local file for the MPRIS `artUrl`, which is why the covers are also
  kept on disk.
- The constructor creates the directory with `QDir::mkpath` and does not check the result. The
  directory comes from `src/app`, which passes a folder inside its temporary directory when it
  runs with read-only settings, and an empty string otherwise.
- `get(url)` returns a null image for an empty URL. Otherwise it looks in memory, then on disk,
  then goes to the network:
  - A memory hit makes the URL the most recently used and returns the image.
  - If `localFile(url)` exists and `QImage` decodes it, the image goes into memory and is
    returned.
  - Otherwise `get` returns a null image and starts a download, unless one for this URL is
    already pending or `networkAccessManager` is null.
- The download is a GET through `networkAccessManager` with `NoLessSafeRedirectPolicy` and
  `setTransferTimeout(kTimeoutMs)`, which aborts it after 20 s without data. When it finishes, the
  URL stops being pending. On an error, or bytes that `QImage::loadFromData` cannot decode, nothing
  else happens. Otherwise the bytes are written to the file exactly as received, the image goes into
  memory, and `ready(url)` is emitted.
- `ready(url)` fires once for each successful download, after the file has been written and the
  image is in memory. A handler's `get(url)` is therefore a memory hit, and `localFile(url)` is set
  if the write worked. It never fires for memory or disk hits, and never on failure.
- The memory holds at most `kMemoryItems` = 30 decoded images, counted by number, not bytes. The
  31st insert evicts the least recently used, where both `get` hits and inserts count as use. This
  class never evicts or expires files on disk.
- Everything runs on the owner's thread. `get` reads and decodes a disk file synchronously, on the
  caller's thread. The reply handler is guarded by a `QPointer`: if the cache is destroyed during a
  download, the result is dropped and the reply is still deleted. The cache does not own
  `networkAccessManager`.

**Traps:**

- The key is the exact encoded URL, so the same cover at another size is another entry and another
  file. The callers (`ui/now_playing_window.cpp`, `integrations/media_controls.cpp`) request
  `SourceRegistry::coverUrl(track, 400)` and compare `ready(url)` with the same URL.
- Failures are not remembered. Once an attempt has finished, every `get` of a failing URL starts a
  new request.
- `localFile()` only checks that the file exists, so it can return a corrupt or half-written file.
  `get` treats such a file as a miss and overwrites it after a successful download.
- A failed file write is silent (no directory, `open` fails, short write). The cover still reaches
  memory and `ready` fires, but `localFile()` stays empty, and so the MPRIS `artUrl` is missing.
- There is no byte limit on the downloaded body, which `readAll()` reads whole, and no size limit
  on the directory.
- With `networkAccessManager == nullptr` the cache serves only files already on disk.
  `tests/mpris_test.cpp` uses this.

## Cover files on disk

`CoverCache` reads and writes one file per cover URL:

```
<directory>/<sha1>.jpg
    <sha1>    40 lowercase hex digits: SHA-1 of QUrl::toEncoded() of the cover URL
    content   the HTTP response body, byte for byte, in whatever format the server sent
```

- A file is written only after `QImage::loadFromData` has accepted the bytes.
- The extension is always `.jpg`, whatever the content is. `QImage` detects the real format;
  anyone else who reads `localFile()` gets the `.jpg` name either way.
- On reading, a file that `QImage` cannot decode is treated as missing. There is no size check.

`Player` reads no files. The audio bytes go straight from the network to the engine.

## Errors

The module has no `Error` class and nothing in it throws or catches, and it calls none of the
loaders that throw (the project's error policy is in
[docs/architecture.md](../../docs/architecture.md#errors)). Every failure is data:

| Where | Failure arrives as |
|---|---|
| no output device | the notice `NoOutputDevice`; the track does not open |
| opening, current track | the failure policy, by the `TrackFailure`'s kind |
| opening, preload | nothing; the preload is dropped |
| a track of no known source | a `TrackFailure{Track}` from `SourceRegistry::open` |
| download, current track | the failure policy; after some audio, the notice `DownloadFailed` and `Skipped` at the end, or for the network a wait |
| download, preload | the preload is cancelled, with no message |
| audio engine | the notice `AudioError` with the engine's message; an undecodable current stream also goes to the failure policy (`AudioEngine::streamUndecodable`) |
| endless source | the `TLoadMoreCallback` reports a failed load as `done({})`; a waiting `next()` stays stopped |
| seek | `seekTo` and `seekFraction` return `false` |
| index out of range | `playIndex` does nothing; `removeTracks` skips it |
| cover | a null `QImage` from `get`, an empty `QString` from `localFile`, and no `ready` |

`TLoadMoreCallback`, `TEventCallback` and the `done` callback run inside Qt slots. Per `CLAUDE.md`,
no exception may cross the event loop, so they must not throw.

## Not here

- Decoding, the blocks of audio, stream buffers, when a queued stream is chained, when
  `trackAdvanced` and `trackFinished` fire, seeking within the downloaded part, and the
  `errorOccurred` texts: [src/audio](../audio/README.md).
- `download-info` and the signed link, the file's HTTP, `Yandex::Track` and its cover and page
  URLs, the `/play-audio` request, and wave sessions and feedback:
  [src/yandex](../yandex/README.md).
- Opening a Yandex track, its play report, the quality, what a pick plays, the vibe's callbacks,
  likes and dislikes, and the jam wave's rotor session:
  [src/sources/yandex](../sources/yandex/README.md).
- The Yandex menu that calls `YandexSource::Catalog`, the status line, the playlist and "Now
  playing" windows: [src/ui](../ui/README.md).
- Media keys, MPRIS and SMTC state, and the cover `artUrl` built from `localFile()`:
  [src/integrations](../integrations/README.md).
- Creating the `Player`, the registry and the cache and choosing the cache directory, loading the
  likes without `autoplay` after login when the queue is empty, and calling `shutDown()` on quit
  before waiting for the last POSTs: [src/app](../app/README.md).
- Tests:
  - `tests/player_test.cpp`: the `Player` on a fake source; every check of a late answer
    (`generation`, `preloadGeneration`, `queueGeneration`) and the rules taken when a track
    starts.
  - `tests/library_test.cpp`: track events, the preload and the gapless advance, re-preloading
    after a queue change, the endless source, and a slow load against a newer choice.
  - `tests/audio_test.cpp`: `playerPollsTheEngine`.
  - `tests/windows_test.cpp`: `nothingPlaysAfterShutDown`.
  - `tests/mpris_test.cpp`: the cache without a network manager.
