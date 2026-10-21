# `src/core` — playback controller, jam mode, lyrics and cover cache

This folder decides what plays. `Player` holds the queue of the current source and moves through it:
play, pause, stop, next, previous, shuffle and repeat. It asks an endless source (a wave) for more
tracks before the queue runs out. For each track it asks the track's source (`TrackSource`, chosen
by `TrackKey::source`) to open it and streams the file's bytes into `Audio::AudioEngine`. It also
preloads the following track, so the engine can go on without a gap, and it reports each track's
start and end: to the source (Yandex's play report) and to the continuation that gave the track, for
feedback. `JamMode` is the jam over the host's own queue: the guests' items as the queue's layer,
the host's source under them (`spec/jam/host.md`). `CoverCache` downloads album covers and keeps
them in memory and on disk. `LyricsFinder` decides which lyrics text the lyrics view shows
(`spec/player/lyrics.md`): it asks the current track's source and, when the user allows it, a lyrics
service, and keeps their answers for the last tracks. The folder does not decide what a pick plays or talk to a source's
catalogue ([src/sources](../sources/README.md)). It does not decode or output audio or chain streams
at the sample level ([src/audio](../audio/README.md)). It speaks to no music service: links, files,
cover URLs, play reports and lyrics are a source's ([src/sources](../sources/README.md)), and the
lyrics service is [src/lrclib](../lrclib/README.md) behind `LyricsService`. It builds no menus
([src/ui](../ui/README.md), `library_menu.cpp`), shows nothing on screen and holds no text for the
user: the `Player` says what happened as `PlayerNotice` codes, and `src/ui` words them. Namespace
`Core`, library `qiyaa_core`.

| File | Contains |
|---|---|
| `player.h/.cpp` | `Player`: the one entry to the queue, transport, shuffle and repeat, source-request tickets, the queue's source (`setSource`: a head and a continuation), track events to the track's source and to the continuation that gave the track, opening tracks through their sources, download streaming, the gapless preload, and the engine's poll timer |
| `queue.h/.cpp` | `Queue`, private to `Player`: the entries, the cursor, what plays next and before (waiting items, the head through the navigator, then the continuation), shuffle and repeat, the title, the layer's seeds, and the load-more generation |
| `track.h/.cpp` | `Track`, `TrackKey`, `CoverRef`, `TrackEvent`: a track of any source |
| `track_source.h/.cpp` | `TrackSource`, what each music source implements; `TrackDownload`, `OpenedTrack`, `TrackFailure`, `TOpenResult`; `SourceRegistry`, the sources by name |
| `lyrics.h/.cpp` | `Lyrics`, `LyricsLine`, `LyricsKind`, `LyricsFrom`, `LyricsAnswer`, `TLyricsDone`: a track's lyrics as a source or a service answers them; `ParseLyricsText` (LRC or plain) and `PlainLyrics`, with the limits |
| `lyrics_finder.h/.cpp` | `LyricsFinder`: the text the lyrics view shows, from the source and the service in their order; `LyricsService`, the port of a lyrics service; `LyricsLookup`, what a lookup ended with |
| `source_notice.h` | `SourceNotice`, `SourceNoticeCode`: what a source's catalogue says about a pick, a like or a dislike, as a code for `src/ui` to word |
| `continuation.h` | `Continuation`, what follows a queue's head: more tracks on request and the events of the tracks it gave; `ContinuationRequest`, `ContinuationReply`, `SeedHint` |
| `queue_source.h` | `QueueSource` (a title, a head, a continuation and the tracks it already gave), `QueueEntry` and `EntryPart`: what a source puts into the queue and what each entry is; `LayerItem`, `QueueRules` and `QueueLayer`: what a layer over the queue (the jam) brings |
| `track_navigator.h/.cpp` | `TrackNavigator`, `ShuffleAlgorithm`, `MakeTrackNavigator`: sequential, random and stored shuffled traversal |
| `failure_policy.h/.cpp` | `FailureKind`, `FailureAction`, `DecideOnFailure`: what the `Player` does about a track that cannot play |
| `jam_mode.h/.cpp` | `JamMode`, `JamEntry`, `JamPlayback`, `IsJamItem`: the jam over the host's own queue, as the `Player`'s layer; it knows nothing of the network, the protocol or Yandex |
| `cover_cache.h/.cpp` | `CoverCache`: cover images by URL, with an LRU of 30 in memory, files in a cache directory and one download per URL at a time |

## Dependencies

- `qiyaa_core` links PUBLIC `qiyaa_audio`, `Qt6::Gui` (`QImage` in `cover_cache.h`) and
  `Qt6::Network` (`QNetworkAccessManager`, `QNetworkReply` in `cover_cache`). It links nothing
  PRIVATE.
- It deliberately does not link `qiyaa_yandex` (a source's business), `Qt6::Widgets`,
  `qiyaa_skins`, `qiyaa_vis`, or anything above it in the order `audio`, `yandex`, `skins` →
  `vis`, `core` → `sources`, `lrclib` → `jam` → `ui`, `integrations` → `app`. It reaches miniaudio only
  through `qiyaa_audio`, which links it PRIVATE. Which modules link `qiyaa_core` is in
  [docs/architecture.md](../../docs/architecture.md#modules).
- `player` and `cover_cache` do not include each other. Only `player.cpp` includes `queue.h`:
  `player.h` declares `Queue` and holds it through a `std::unique_ptr`, so nothing outside the
  `Player` reaches the queue but through it.
- No file includes `yandex/`. The only HTTP of `core` is `CoverCache`'s: a track's file comes
  through its source's `TrackDownload`, or as the `Audio::ByteSource` of a file on disk.
- `Player` is the only caller of `AudioEngine::poll()` in `src/`. Everything the engine reports
  asynchronously therefore arrives through `Player`'s timer (see [Threads and
  lifetime](#threads-and-lifetime)).

```bash
grep -rnE --include='*.h' --include='*.cpp' '#include "(sources|lrclib|jam|ui|vis|skins|integrations|app)/' src/core/   # must print nothing
grep -rnE --include='*.h' --include='*.cpp' '#include <(QWidget|QApplication|QDialog|QMenu|QtWidgets)' src/core/   # must print nothing
grep -rnwE --include='*.h' --include='*.cpp' 'throw|catch' src/core/                                    # must print nothing
grep -rnE --include='*.h' --include='*.cpp' '\btr\(|translate\(' src/core/                                # must print nothing
grep -rn --include='*.h' --include='*.cpp' -e '->poll()' src/ | grep -v '^src/core/player.cpp:'         # must print nothing
grep -rn --include='*.h' --include='*.cpp' '#include "yandex/' src/core/                               # must print nothing
grep -rnE --include='*.h' --include='*.cpp' 'QNetworkRe(quest|ply)' src/core/ | grep -v '^src/core/cover_cache\.'   # must print nothing
grep -rln --include='*.h' --include='*.cpp' '#include "core/queue.h"' src/ | grep -vx 'src/core/\(player\|queue\)\.cpp'   # must print nothing
grep -rn --include='*.h' --include='*.cpp' --include='*.mm' 'statusMessage' src/                       # must print nothing
```

## `player.h`: `Player`

```cpp
class Player : public QObject {
public:
    using TrackEvent = Core::TrackEvent;
    enum class State { Empty, Stopped, Loading, Playing, Paused, WaitingForNetwork, WaitingForMore };

    Player(Audio::AudioEngine* engine, SourceRegistry* sources, QObject* parent = nullptr);

    quint64 newSourceRequest();                       // new ticket; every older one stops being the latest
    bool isLatestSourceRequest(quint64 ticket) const;

    void setSource(QueueSource source, bool autoplay);   // autoplay: playIndex(0) right away
    void removeRows(QList<int> rows);                 // rows before removal, any order, duplicates ok; HOST-36
    void clear();
    void setLayer(std::optional<QueueLayer> layer);   // the jam's; nullopt ends it
    void setLayerItems(const QList<LayerItem>& items, std::optional<SeedHint> hint);   // the layer's items now
    void setHistoryWaveFactory(THistoryWaveFactory factory);   // the vibe from history of each new queue
    void setContinueWithVibeByDefault(bool on);       // the setting: each new queue's flag (CONT-01)
    void setContinueWithVibe(bool on);                // the current queue's flag only (CONT-02)
    bool continuesWithVibe() const;
    bool hasOwnContinuation() const;                  // the source has one (My Vibe, a station)
    bool canSwitchVibe() const;                       // the flag applies: no own continuation, a vibe allowed

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
    bool shuffleActive() const;                       // shuffle && !hasOwnContinuation()
    bool repeat() const;
    const QueueRules& rules() const;                  // QueueRules{playReports, previousRestartsOnly}
    State state() const;                              // see Phases and State
    double positionSeconds() const;                   // the engine's position
    QUrl currentLink() const;                         // the current track's file; empty until it opens
    QUrl nextLink() const;                            // the preloaded file; empty without a preload

    const QList<QueueEntry>& entries() const;
    const QString& queueTitle() const;
    int currentIndex() const;                         // -1: nothing is current (see the cursor)
    const QueueEntry* currentEntry() const;           // nullptr when currentIndex() is -1
    const Track* currentTrack() const;                // the current entry's track
    bool hasNext() const;                             // Queue::next() has an entry
    int currentBitrate() const;                       // kbps of the current file; 0 until it opens
    double durationSeconds() const;                   // Track::durationMs / 1000; 0 without a track
    int preloadedIndex() const;                       // -1 until the preload has an engine stream

    void setNetworkOnline(std::optional<bool> online); // the system's view; nullopt: cannot tell
    void setNetworkRetryDelays(int firstMs, int maxMs); // 2 s doubling to 60 s by default

Q_SIGNALS:
    void noticed(const Core::PlayerNotice& notice);   // see Notices
    void failed(Core::FailureKind kind);              // every failure the policy decides on
    void trackEvent(Core::TrackEvent event, const Core::Track& track, double playedSeconds);   // every track's
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
  thread. A `Continuation` is called on it and must call `done` on it too.
- The engine emits `trackFinished` and `trackAdvanced` only from `AudioEngine::poll()`. The only
  caller of `poll()` is `Player`'s own `QTimer` (100 ms interval). The timer runs whenever the
  engine state is not `Stopped` (it follows `AudioEngine::stateChanged`), so the end of a track
  is noticed with every window hidden or minimised. Each tick runs `poll()` first, together with
  any track change it triggers, then adds to the played seconds, then emits `positionTick`.
- `Player` does not own the `AudioEngine` or the `SourceRegistry`, and neither pointer is checked
  for null. Both, and the sources in the registry, must outlive the `Player`.

### The queue and the cursor

- `setSource` puts the source's head into the queue as `Head` entries and the tracks its
  continuation already gave (`QueueSource::given`, a vibe's first batch) after them as
  `Continuation` entries, each with that continuation as its giver. Every way into the queue keeps
  only tracks with `Track::available == true`; indices always refer to this filtered list.
- `currentIndex()` is a cursor. It is -1 when the queue is empty, when it holds only waiting
  items (a layer's items that came into an empty queue), and after the current entry was removed
  with a waiting item behind it; `next()` and `play()` then play the first waiting item. Otherwise
  it is a valid index, also while nothing plays. `setSource` sets it to 0 and plays only with
  `autoplay`. A continuation's first tracks in an empty queue move it from -1 to 0 and do not
  play.
- `removeRows` takes rows of the queue before the removal. Duplicates and out-of-range rows are
  ignored. Removing rows before the cursor shifts the cursor down. If the current entry is
  removed, playback stops (it does not skip), and the cursor moves to the entry that followed it,
  or to the new last one. A waiting item that followed it does not become current: it goes on
  waiting, the cursor goes to -1, and the item starts, with its `Started`, only by Play or Next
  (HOST-36).
- `clear()` takes a new ticket and calls `setSource({}, false)`. This also drops the continuation.
- The layer is in [The layer](#the-layer).

### Phases and `State`

What the `Player` knows about the track that plays is one value: the track, whether its `Started`
went out, the callback and the `QueueRules::playReports` taken at that moment, its link and
bitrate, its feed (the engine stream, the download and how it ended, whether audio arrived), a
network wait and the played seconds. A stop drops it whole, so nothing of a stopped track lingers:
`currentLink()` is empty and `currentBitrate()` is 0 (`currentTrack()` is the cursor's and stays).

The current track's phase:

| Phase | `state()` | `Started` sent | Entered by | Left by |
|---|---|---|---|---|
| **Empty** | Empty | — | `setSource` with no available track, `clear`, removing every row | `setSource` with tracks, a continuation's tracks or the layer's items (to Stopped) |
| **Stopped** | Stopped | — | `stop`, `setSource` without `autoplay`, the end of a finite queue, removing the current track, a failure the policy stops on (see [Failures](#failures-during-playback)), `playIndex` without an output device | `play`, `playIndex`, `next`, `previous` |
| **Opening** | Loading | no | `playIndex` without a usable preload: the engine stream is begun and the source asked | opened: **Open**. A `TrackFailure`: the [failure policy](#failures-during-playback). `stop`, `playIndex`, `setSource`: the answer is dropped |
| **Open**, downloading | by the engine¹ | yes | the track opened and its download started, or the preload was taken over while still downloading | download OK: **Open, downloaded**. Download failed: the failure policy at once if no audio arrived, else **Open, failed**. End or skip: see [Inputs](#inputs-in-the-order-they-act) |
| **Open**, downloaded | by the engine¹ | yes | the current download finished without error, or a finished preload was taken over | end or skip. Only in this phase can a new preload start |
| **Open**, failed | by the engine¹ | yes | the current download failed after some audio arrived; the engine plays what arrived | end: a network failure **Waiting for the network**, any other failure the end reported as `Skipped`. Skip |
| **Waiting for the network** | WaitingForNetwork | as before | a network failure (ERR-01 to ERR-03) | the track opens and its download starts: **Open** again, playing if it played before. Another failure: the policy. `stop`, `playIndex`, `setSource`: the wait ends |
| **Waiting for more** | WaitingForMore | — | `next()` at the end of an endless queue, while a request is in flight | `done` with new tracks: `playIndex(Queue::next())`. `done({})`: Stopped (WAVE-09). `play`, `playIndex`, `stop` or `setSource` end the wait |
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
the track fails to open and when its audio runs out, ERR-03); Play without an output device gives
none, as the state stays Stopped (`player_test`).

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

- **`setSource(source, autoplay)`**
  1. `stop()`: the open track gets `Skipped` (to its source and to the continuation that gave it),
     and the current track (its open answer, download and wait) and the preload are dropped.
  2. The queue becomes the source's entries, and the title and the continuation are replaced. The
     rules go back to the defaults, the load-more and waiting flags are reset, and a new load-more
     generation starts.
  3. The cursor goes to 0, or to -1 when the queue is empty.
  4. `queueReplaced`, then `playlistChanged`, then `currentTrackChanged`.
  5. With a continuation: `ShuffleOffInVibe` if shuffle is on, then `continuation->begin(true)`
     (a vibe's `radioStarted`, TRK-03).
  6. If `autoplay` is set and the queue is not empty: `playIndex(0)`.
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
     `stop`, `playIndex` or `setSource` is dropped, and its download never starts. A `TrackFailure`:
     the failure policy. Opened: the download is connected to the stream begun above and started,
     then the start.
- **The start** (private): stores the bitrate, takes the entry's giver (a `Continuation` entry's)
  and `QueueRules::playReports` for all of the track's events, resets the played seconds, sends
  `Started(track, 0)` to the source (with `playReports`), then to the giver, then as the signal
  `trackEvent`, emits `currentTrackChanged` and calls `maybePreload()`.
- **`next()`**: with an empty queue, nothing happens. If a preload exists in any phase:
  `playIndex(preload index)`. Else if `Queue::next() >= 0`: `playIndex` of it. Else `stop()`, and
  the cursor stays on the last track. Then, with a continuation to ask (see
  [Continuations](#continuations)): the stall ends (CONT-07, WAVE-09), the player marks waiting and
  runs the load-more check. A request in flight gives the notice `LoadingMore`; none (nothing to
  ask with, CONT-06) ends the wait, Stopped. The automatic advance (a track's end, a failure the
  policy skips) is the same, except that it leaves a stall alone, so it stops there (TR-04).
- **`previous()`** (spec TR-01, TR-02): more than
  `kPreviousRestartsAfterSeconds` (3 s) into a playing or paused track, it restarts that track
  with `seekTo(0)`. That is a seek, not a new start: no events and no play report; if the engine
  refuses the seek, `playIndex(cursor)`. Earlier, or while stopped: `playIndex(Queue::previous())`.
  On a head entry that is the navigator's previous head entry: in source order, at the first one
  the last with repeat and itself without; with shuffle the stored order for WithoutRepeats,
  source order for Random. On any other entry it is the row before, or the entry itself on row 0;
  when the row before is a head entry, it is the head entry that became current last, the head's
  last track in play order (SHUF-02). A step back from a continuation's entry to an entry of
  another kind moves the continuation's mark back before it (`Queue::stepBack`), so Next returns
  to it.
- **`play()`**, by engine state: Paused resumes. Playing calls `playIndex(cursor)`, which restarts
  the track as Winamp does, with `Skipped` and then a new `Started`. Buffering does nothing.
  Stopped calls `playIndex(cursor)`; when the cursor is -1, `playIndex(Queue::next())`, or row 0
  when that is -1 too.
- **`pause()`**: Paused resumes. Playing or Buffering pauses. Stopped does nothing.
- **`stop()`**: sends `Skipped` for the open track, drops the current track whole, ends the wait
  for more, cancels the preload and stops the engine. It keeps the queue, the cursor and the
  continuation. Of its own signals only `stateChanged` follows, after the pass; the engine
  emits `stateChanged(Stopped)`, and that stops the timer.
- **Engine `trackFinished`**: when the download broke on the network after audio arrived, the track
  stays open and waits for the network (ERR-03). Otherwise it gets `Finished`, or `Skipped` if its
  download failed, with the played seconds. Then, if `Queue::next()` is the current entry (repeat
  with one head track and nothing waiting), `playIndex(cursor)`; otherwise the automatic advance.
- **Engine `trackAdvanced`** (the engine crossed into the queued stream with no gap): the open track
  gets `Finished` or `Skipped` as above. If there is no preload, or its stream is not the engine's
  `currentStream()`, or its index is out of range, the player resyncs:
  `playIndex(Queue::next())`, or `stop()` when that is -1. A cancelled preload is always cleared
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
- **A continuation's reply**: see [Continuations](#continuations). The available tracks become
  that continuation's entries at the end. If none were added, no signal is emitted. Otherwise the
  cursor moves from -1 to 0 if the queue was empty, then `playlistChanged`, then
  `refreshPreload()`.
- **`removeRows`**: see [the queue](#the-queue-and-the-cursor). If the current entry was
  removed: `stop()`, then `currentTrackChanged`. Then, always, `playlistChanged` and
  `refreshPreload()`.
- **`SourceRegistry::filesChanged(source)`** (a source's quality changed): if the preload is
  another source's track, nothing. Otherwise `cancelPreload()`, then `maybePreload()`, so a next
  track fetched in the old quality is fetched again (spec QUAL-05). The current track goes on with
  its file; no signal is emitted.
- **`setShuffle`, `setRepeat`**: nothing happens without a change. With a change:
  `modesChanged`, then `refreshPreload()`. Switching shuffle on while the queue is endless also
  gives the notice `ShuffleOffInVibe`; `setSource` gives the same when an endless queue starts
  with shuffle on.
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
| `sourceRequest` (the ticket) | `newSourceRequest()`: callers before a source load, `clear`, `shutDown` | a caller's source-load reply, when the caller checks `isLatestSourceRequest(ticket)`. `Player` never checks it itself |
| the load-more generation (`Queue::generation`) | `setSource` (and so `clear`) | a continuation's `done` for a queue or a base that has been replaced (a `QPointer` also covers a destroyed `Player`) |
| `generation` | closing the current track (`stop`, `playIndex`, a track's end), opening it, the network wait, a preload taken over | the open answer of a track that is no longer current |
| `preloadGeneration` | every new preload | the open answer of a preload that was cancelled or replaced |

Tickets start at 1, so 0 never matches. `YandexSource::Catalog` takes one for every pick. The
next step of [#23](https://github.com/Kickoman/QiYaa/issues/23) makes the phases a
`std::variant`, each answer bound to the phase that asked; the counters then go.

### Continuations

- The continuation the queue asks is its source's own (`QueueSource::continuation`); else, while the
  queue's flag "continue with a vibe" is on, the **vibe from history**, which
  `setHistoryWaveFactory`'s factory made for this queue (spec
  [continuation.md](../../spec/player/continuation.md)). A queue with its own is endless: at its end
  `Queue::next()` returns -1 even with repeat on, so it never wraps, and `next()` waits for more
  instead. With the vibe from history, repeat wraps the head, and the vibe waits behind it
  (CONT-10).
- Each new queue, and each base under a layer, takes the flag from
  `setContinueWithVibeByDefault` (true unless set; CONT-01), forgets the tracks that started and
  the stall, and gets a new vibe from history. `setContinueWithVibe` changes the current queue's
  flag only and emits `modesChanged`; switched on, it ends the stall and runs the check at once
  (CONT-02, CONT-03).
- A source with `QueueSource::waveFromHistory = false` (local files, CONT-12) gets no vibe from
  history at all, also under a layer: the flag is off whatever the setting, and
  `setContinueWithVibe` does nothing. `canSwitchVibe()` is false for it, as for a source with a
  continuation of its own, and the menu item follows it. A later queue, a cleared base included
  (`QueueSource{}`), takes the flag and the vibe again.
- The load-more check (`maybeLoadMore`) asks that continuation when all of these hold: there is
  one, no request is in flight for it, and `Queue::remaining() <= kLoadMoreWhenLeft` (2).
  `remaining()` counts in play order: the current entry, the waiting items, what is left of the
  head (endless while the base is in its head with repeat on, or with Random and two or more head
  tracks) and the continuation's entries after the last one that became current.
- The request carries `queueEnd`, the last 5 tracks of the queue, whatever gave them (WAVE-05);
  `history`, the tracks of this queue that started and that no continuation gave, the latest start
  first, each once (CONT-05); `jamSeeds`, the layer's `SeedHint` while one is on; and `feedback`,
  the layer's rule for that continuation (true without a layer).
- `more` returns `false` when it has nothing to ask with; then no `done` comes and the request is
  not in flight.
- The check runs when a track becomes current (`playIndex` in both branches, before the link is
  resolved, and the gapless advance); when a track starts, if the vibe from history is what the
  queue asks (a seed comes with a start, CONT-04); when `next()` reaches the end; when the flag is
  switched on; and after `setLayerItems` unless the state is Empty or Stopped. It does not run from
  `setSource` without `autoplay`, `removeRows` or another mode change.
- **The stall** (CONT-07): when the vibe from history replies with no available track before it
  ever gave one (its first request failed or brought nothing), the check stops asking: the head
  plays to its end and stops there. A user's `next()` at the end, switching the flag on and a new
  queue end it.
- `done(reply)` is ignored if the `Player` is gone or the queue has been replaced. Otherwise the
  request is no longer in flight. A reply of the vibe from history that the seeds outdated is
  dropped and the check runs again ([The layer](#the-layer)); a waiting `next()` that asks nothing
  then stops. A `failure` gives the notice `VibeFailed`.
  The available tracks of `reply.tracks` are appended as that continuation's entries, with the
  reply's `seedsVersion`. If `next()` was waiting and `Queue::next()` now has an entry, `playIndex`
  of it runs; if nothing came, the player is Stopped at the end (WAVE-09), and only the next
  `next()`, or the layer's items, go on from there.
- `done` may be called synchronously from inside `more`.

### Next, shuffle, repeat

- What `Queue::next()` gives, in this order: the first waiting item; else, while the base is in its
  head, the navigator's next head entry (repeat wraps the head when the source has no continuation
  of its own); else the continuation's entry after the last one that became current; else -1. A
  head with a continuation therefore plays the head first and the continuation after it, and an
  `Other` entry or an item that already played comes back only when picked.
- The navigator covers the head entries only, by their order among them. Selecting a head entry
  selects it in the navigator, selecting a continuation entry moves the continuation's mark, and
  an item stops waiting. After every selection the waiting items move right behind the new current
  entry (HOST-12), so the playlist shows them where they play; `select` returns the row after the
  move.
- Shuffle does not apply when the source has a continuation of its own (spec WAVE-10, WAVE-11): a
  vibe always uses sequential traversal. With the vibe from history it orders the head only, and
  the vibe's tracks follow in their order (CONT-09); Random never runs out of head tracks, so the
  vibe from history never starts. The shuffle button and preferred algorithm retain the user's
  choices.
- `shuffleAlgorithm()` defaults to WithoutRepeats. The application restores and saves it. Switching
  algorithms while shuffle is active rebuilds navigation and refreshes preload; while inactive it
  changes only the preference. Audio, position and pause state are preserved in either case.
- A new source, removing a head row, or switching shuffle resets the navigator from the last head
  entry that became current (or from before the head's first, under a layer). Such a reset starts
  a new shuffled pass, which can include tracks heard before. The layer's items, a continuation's
  tracks and jumps keep the pass. A direct `playIndex` selects the entry in the existing order.
- `next()` and preload read the same next entry. Looking ahead never consumes it. Explicit Next
  and gapless advance call `select` when a track becomes current.
- Repeat with a single track replays it, including through gapless preload. An endless queue with
  one track does not replay it: it waits for more (spec WAVE-12).

### The layer

A layer lies over the queue's own source, the base, while it is on: the jam
([`JamMode`](#jam_modeh-jammode), spec/jam/host.md "The base").
- `QueueLayer::rules` are `rules()` while the layer is on, for every track that starts:
  `playReports = false` keeps the source from hearing them (HOST-15), `previousRestartsOnly` makes
  Previous a restart (HOST-07), `ownContinuationFeedback = false` keeps a vibe the base has of its
  own from hearing its tracks (HOST-15), and `historyFeedback` says whether the vibe from history,
  which is the jam vibe then, does (HOST-16).
- `setLayerItems(items, hint)` makes the waiting items match `items` with the smallest change
  (HOST-01 to HOST-04): the longest common prefix of the waiting items and `items` stays, with its
  preload, the rest goes, and what differs comes in right behind the current entry, in front of the
  rest of the base (HOST-12). Only items with an item id are the layer's: items an earlier layer
  left waiting are the base's now, and the new ones come in front of them. The current item is left
  out even while `items` still lists it. Items come in as `Item` entries that wait, available
  whatever their `Track::available` says. Nothing starts; HOST-08 is the caller's.
- `SeedHint` is the server's seeds and their version. While the layer is on, the vibe from history
  is asked with them (`ContinuationRequest::jamSeeds`) and replies with the version its session
  started with. When the hint has seeds (HOST-11):
  - a reply of the vibe from history with another version, or with none (a session on the
    history's seeds), is dropped, and the check runs again;
  - when no item waits and the current entry is not a track of that vibe, its entries after the
    continuation's mark with another version go, after `setLayerItems` and after every selection;
    a head track and the base's own vibe's never do;
  - a hint with a new version ends the stall (CONT-07); the request goes at the next check.
- `setSource` under a layer replaces the base only (HOST-21). The current entry stays (an item as
  an item, anything else as `Other`), the layer's waiting items stay behind it, and the new head
  and continuation follow; played rows and the old base go, items an earlier layer left waiting
  among it, and the new base takes the flag of a new queue. Nothing stops or starts (`autoplay` is
  ignored), the base begins before its head's first track, and its continuation gets
  `begin(ownContinuationFeedback)` (HOST-15).
- `removeRows` under a layer (HOST-36) hands the waiting items' ids to `QueueLayer::removeItems`
  (the server removes them, and the next state takes them out) and removes the other rows at once,
  items an earlier layer left among them.
  `clear()` is `setSource({})` under it: the current entry and the layer's waiting items stay.
- `setLayer(std::nullopt)` ends the layer (HOST-32, HOST-33): every item loses its item id, the
  waiting ones still play first, the entries of the vibe from history that came on the server's
  seeds and have not played go, and the rules return to the defaults. The waiting items do not join
  the head, a departure from HOST-32's "join the head, in front of its rest": they play once, before
  the head goes on, repeat does not play them again, and shuffle does not take them into its pass.
  The base's own continuation and the vibe from history get `begin(true)`, so what began without
  feedback during the jam sends `radioStarted` before the feedback of its next track. A reply in
  flight from the server's seeds is dropped when it comes (no hint: any version is stale).

### Downloads

- The file comes from the `TrackDownload` the source opened. The `Player` connects it to its
  stream before it calls `start()`.
- A source may open a file on disk instead (`OpenedTrack::file`). The `Player` then hands it to
  the engine, `AudioEngine::attachSource` for the current track and `queueStream(file)` for a
  preload, and takes it as downloaded at once: a preload is asked for as soon as the track starts.
- Every `received` passes the bytes to `AudioEngine::appendData(stream, …)`, with the stream id
  captured at the start. `finished` calls `finishData(stream)`, or `failData(stream)` with a
  `TrackFailure`. The engine ignores data for streams it has dropped.
- `Player` sets no byte limit. Every byte goes to the engine, which keeps the stream in memory; a
  file on disk is read from the disk.
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
  stops at once. The count goes back to 0 when the engine reaches Playing, and in `setSource`
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
  set that). `stop`, `playIndex` and `setSource` end the wait.
- `src/app` feeds `setNetworkOnline` from `QNetworkInformation` when a backend can tell
  reachability; without one the timer alone ends a wait.

### Gapless preload

- **Started** by `maybePreload()` when all of these hold: there is no preload; the player is not
  shut down; a track is open and its download has finished without error; the engine is not
  `Stopped` (Paused counts); and `Queue::next() >= 0`. It is called at the end of the start, when
  the current download finishes, and from `refreshPreload()`.
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
- **Refreshed** by `refreshPreload()` after every change of the entries outside `playIndex` (a
  continuation's reply, `removeRows`, the layer's items, its start and end, a base under a layer)
  and any actual change of shuffle or repeat. The preload is kept, and its index updated, when its
  track id is still what follows at `Queue::next()`, in either mode. Otherwise the preload is
  cancelled. Whenever no preload is kept, `maybePreload()` tries a new one.
- A queued stream that the engine cannot decode is forgotten by the engine, and
  `queuedStream()` returns 0 for it. The next `playIndex` of that track then fetches it again the
  ordinary way.

### Track events and play reports

- Every track that opens gets `Started(track, 0)`, followed by exactly one `Finished` or
  `Skipped` with its played seconds. Each event goes, in this order, to the track's source (unless
  `QueueRules::playReports` was false when the track started), to the continuation that gave the
  track (its entry's giver, taken at the start, even after that continuation was replaced; a head
  track has none), and out as the signal `trackEvent`. The closing event is always sent before the
  `Started` of the next track, because `playIndex` and both engine handlers close the open track
  first.
- `Started` is sent:
  - after the track opens and its download starts, before any audio of it is heard;
  - at once, when the preload is taken over by `playIndex` or `next()`;
  - at the gapless boundary.
- `Finished` is sent when the engine reports the track's end (`trackFinished` or `trackAdvanced`)
  and its download did not fail.
- `Skipped` is sent in every other case: `stop`; `playIndex`, `next` or `previous`; `play` while
  playing; `setSource` without a layer, or `clear`; removing the track; `shutDown`; or an end after
  a failed download, where the track only drained to where the data stopped.
- No event is sent for a track that failed to open, because it never opened; nor without an output
  device, where no track opens. None is sent when the `Player` is destroyed with a track open
  either; call `shutDown()` first, as `App::Application::quit` does. An open answer that arrives
  after the `Player` is gone is dropped (every answer holds a `QPointer` to it).
- The played seconds are the seconds actually heard. On every tick and at close, the position
  step since the previous reading is added if the engine is `Playing` and `0 < step < 1.0`.
  Normal steps are about 0.1 s. Seeks and other jumps of 1 s or more, backward moves, and paused
  or buffering time do not count. `seekTo` also moves the baseline to the target.
- The source hears its tracks' events through `SourceRegistry::trackEvent`; for a Yandex track
  `Started` is the play report. A jam turns that off with `QueueRules::playReports` (HOST-15).
- With `QueueRules::previousRestartsOnly` (the jam, HOST-07), `previous()` always restarts the
  current track by `seekTo(0)`, or `playIndex(cursor)` when the engine refuses the seek.

### Signals

| Signal | Emitted |
|---|---|
| `noticed(notice)` | see [Notices](#notices) |
| `stateChanged(state)` | after a pass of the event loop in which `state()` changed; see [Phases and `State`](#phases-and-state) |
| `playlistChanged` | by `setSource`; by a continuation's reply when something was added; by `removeRows` always, even when no row was valid; by `setLayer`, `setLayerItems` and `playIndex` when the entries changed (items came, went, moved or stopped waiting; stale wave tracks went) |
| `queueReplaced` | by `setSource` and `clear` only, before `playlistChanged` |
| `trackEvent(event, track, seconds)` | for every track's `Started` and its closing event, after its source and its giver |
| `currentTrackChanged` | by `setSource`, even for an empty queue (under a layer only when the cursor moved); by `playIndex` at once, also without an output device; again at the start, once the bitrate is known; twice on a gapless advance; by `removeRows` when the current entry was removed |
| `positionTick` | after each timer tick's `poll()`, every 100 ms while the engine is Buffering, Playing or Paused |
| `modesChanged` | by `setShuffle`, `setShuffleAlgorithm` and `setRepeat` on a change; by `setLayer`, and `setSource` under a layer |
| `seeked(target)` | after a successful `seekTo` or `seekFraction`, with the clamped target |

### Notices

```cpp
enum class NoticeCode { ShuffleOffInVibe, AudioError, NoOutputDevice, LoadingMore, DownloadFailed,
                        TrackFailed, AccessDenied, StoppedAfterFailures, WaitingForNetwork, VibeFailed };
struct PlayerNotice {
    NoticeCode code;
    QString detail;           // English, for AudioError, DownloadFailed, TrackFailed, AccessDenied, VibeFailed
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
| `LoadingMore` | `next()` at the end of an endless queue, with a request in flight |
| `DownloadFailed` | the current download failed after some audio arrived, not on the network |
| `TrackFailed` | the policy skips the track or stops on it (ERR-04, ERR-07) |
| `AccessDenied` | an Auth failure (ERR-08) |
| `StoppedAfterFailures` | the third track in a row did not play (ERR-05) |
| `WaitingForNetwork` | a network wait begins (ERR-01 to ERR-03) |
| `VibeFailed` | a continuation's reply says its request failed; `detail` is its text (CONT-07) |

**Traps:**

- `pause()` toggles, and `play()` restarts the track while it is playing. Code that serves media
  keys has to check the state first, as `src/integrations` does.
- `setSource` ignores tickets. An asynchronous source load must take `newSourceRequest()` before
  the request and drop the reply unless `isLatestSourceRequest(ticket)`. A direct `setSource` does
  not invalidate loads in flight; only a new ticket does. That is why `clear` and `shutDown`
  call `newSourceRequest()` and throw the ticket away. Do not remove those calls.
- A continuation that returned `true` from `more` must call `done` exactly once, on the `Player`'s
  thread, with no tracks on failure. Until it does, that queue never asks again. After an empty
  reply, a waiting `next()` ends Stopped; the next `next()` asks again. If `done` runs
  synchronously inside `more`, playback has already moved on, and `next()` gives no
  `LoadingMore`.
- A continuation's `more` and `trackEvent`, and the `trackEvent` signal's slots, run synchronously
  inside `Player` methods and the timer's handler. They must not call `playIndex`, `stop` or
  `setSource`, and they must not throw (see [Errors](#errors)).
- A source may answer `open` after the `Player` is gone: every reply checks a `QPointer` to it.
- The poll timer calls `accumulatePlayedSeconds()` for its side effect; dropping the call loses
  the played time. A stall of the GUI thread longer than 1 s between two ticks also loses that
  stretch.
- `durationSeconds()` is `static_cast<double>(durationMs) / 1000.0`, using the metadata duration,
  not the decoded length. `seekTo` clamps to it.
- Removing a row before the current one changes `currentIndex()` without
  `currentTrackChanged`. The same is true when a continuation's first tracks move the cursor from
  -1 to 0, and when a jump moves waiting items from before the new current entry. UI that depends
  on the index must also listen to `playlistChanged`.
- Without an output device no track opens: Play only moves the cursor and gives
  `NoOutputDevice`. Nothing reaches the source or the continuation, and the load-more check still
  runs.
- `shutDown()` blocks playback only. `setSource`, `removeRows` and the layer still change the
  list, and `next()` at the end of an endless queue still asks the continuation.
- `Started` means "the download began", not "audio is audible".
- `stateChanged` comes a pass after the change. Code that acts right after a call reads `state()`,
  which is current at once; a listener gets one signal per settled state, not one per engine
  transition.
- The timer starts on the engine's next `stateChanged`. An engine that is already playing when
  the `Player` is constructed is not polled until its state changes.

## `queue.h`: `Queue`

```cpp
class Queue {                                         // owned by Player; no QObject, no engine
public:
    const QList<QueueEntry>& entries() const;
    const QString& title() const;
    int cursor() const;                               // -1: nothing is current
    const QueueEntry* currentEntry() const;
    const std::shared_ptr<Continuation>& continuation() const;   // the source's own
    quint64 revision() const;                         // changes with every change of the entries
    void replace(QueueSource source);                 // a new queue and generation
    void replaceBase(QueueSource source);             // under a layer: the current entry and the layer's items stay
    bool appendContinuation(const QList<Track>& tracks, const std::shared_ptr<Continuation>& giver,
                            std::optional<int> seedsVersion);   // false: none available
    bool remove(QList<int> indices);                  // true: the current entry went
    void setItems(const QList<LayerItem>& items, std::optional<SeedHint> hint);
    const std::optional<SeedHint>& seeds() const;
    void endLayer();
    bool isStale(std::optional<int> seedsVersion) const;   // see The layer
    // the vibe from history, whether it gave a track, the flag, the tracks that started, the stall
    int select(int index);                            // the row it is on after waiting items moved
    int next() const;                                 // -1: nothing follows
    int previous() const;
    int stepBack();                                   // previous(), and the continuation's mark moves back
    int firstToPlay() const;                          // the cursor, or with none the first next() gives
    int remaining() const;                            // in play order; kEndlessTracks
    // shuffle, its algorithm, repeat; the load-more generation and whether a load is in flight
};
```

- What plays next and before is the `Queue`'s; playing it is the `Player`'s. The `Player` calls
  the `Queue` synchronously in the middle of its own transitions, so the two never signal each
  other. The `Player` emits `playlistChanged` when `revision()` moved.
- The order is that of [Next, shuffle, repeat](#next-shuffle-repeat), the layer's part that of
  [The layer](#the-layer), the cursor rules those of
  [The queue and the cursor](#the-queue-and-the-cursor).
- The base's place is two marks: the last head entry and the last continuation entry that became
  current, by their order among their part's entries, and whether the base is in its head. A
  removal before a mark moves it back, so what followed still follows.
- `replace`, `replaceBase` and `renew` start a new load-more generation, which drops a late answer
  of a continuation asked before.
- Tests: `tests/queue_test.cpp`.

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
    std::unique_ptr<TrackDownload> download{};    // not started yet
    QUrl link{};                                  // what a jam's guests may fetch (about an hour)
    int bitrateKbps = 0;
    QString codec{};                              // "mp3", "he-aac-mp4"…
    std::shared_ptr<Audio::ByteSource> file{};    // or this instead of a download: a file on disk
};
using TOpenResult = std::variant<OpenedTrack, TrackFailure>;

class TrackSource : public QObject {
public:
    using TOpened = std::function<void(TOpenResult result)>;
    virtual QUrl coverUrl(const Track& track, int pixels) const = 0;   // empty: no cover
    virtual QUrl webPage(const Track& track) const = 0;                // empty: no web page
    virtual void open(const Track& track, TOpened done) = 0;
    virtual void trackEvent(TrackEvent event, const Track& track, double playedSeconds) = 0;
    virtual void lyrics(const Track& track, TLyricsDone done) = 0;
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
    void lyrics(const Track& track, TLyricsDone done);
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
- An `OpenedTrack` carries exactly one of `download` and `file`; with a `file` the `Player` does
  not look at `download`.
- A `TrackDownload` emits nothing before `start()` and never inside it. `finished` comes once and
  last; a `TrackFailure` there is the download's (a cut connection is `Network`). Deleting a
  download aborts it, and nothing is emitted after that. The `Player` never deletes one inside its
  own signal.
- `OpenedTrack::codec` says what the file is, for showing it, as #27 asks of the port. Nothing
  shows it yet; only `yandex_source_test` reads it.
- `trackEvent` takes every event of a track that started with `QueueRules::playReports`; what the
  source does with it is its own (Yandex reports `Started` only).
- `filesChanged`: the source now opens other files for the same tracks (another quality). The
  registry re-emits it with the source's name.
- `lyrics` answers with the track's lyrics as the source has them ([`lyrics.h`](#lyricsh-lyrics)),
  under the same rules as `open`: `done` exactly once, from the event loop, never inside the call,
  and a port destroyed first may drop it. The asker may be gone by then: `LyricsFinder` checks a
  `QPointer` to itself in every `done` it hands out. A port answers `NoAccount` when it would need
  a login it does not have, `Failed` when asking again later may help, and `Missing` otherwise.
- The registry routes by `track.key.source`. A track of a source it does not know fails to open
  with `TrackFailure{Track}` on the next pass of the event loop, its events go nowhere, and its
  lyrics are `Missing` on the next pass.
- Like, dislike and the other capabilities of a source are not in the port yet: the Yandex menu
  asks `YandexSource::Catalog`.

## `source_notice.h`: `SourceNotice`

```cpp
enum class SourceNoticeCode { Loading, Searching, Failed, VibeFailed, SearchFailed, Empty, NothingFound,
                              Loaded, LoadedFirst, AddedToLiked, RemovedFromLiked, Disliked };
struct SourceNotice {
    SourceNoticeCode code;
    QString subject;          // the pick's title (a search's text for Searching)
    QString detail;           // English: the request's error
    int count = 0;            // Loaded: the available tracks; LoadedFirst: those taken, the rest left out
};
```

A catalogue tells what became of the user's pick as a code, never as text, as the `Player` does
with `PlayerNotice`: `YandexSource::Catalog::noticed`, worded by `Ui::SourceNoticeText`. `subject`
is a title the catalogue gives the queue, already in the user's language; `detail` is not
translated.

## `continuation.h`: `Continuation`

```cpp
struct SeedHint { QStringList seeds; int version = 0; };   // the jam server's seeds
struct ContinuationRequest { QList<Track> queueEnd; QList<Track> history; std::optional<SeedHint> jamSeeds;
                             bool feedback = true; };
struct ContinuationReply { QList<Track> tracks; QString failure; std::optional<int> seedsVersion; };
class Continuation {
public:
    using TDone = std::function<void(const ContinuationReply& reply)>;
    virtual void begin(bool feedback) {}                   // the queue took this source
    virtual bool more(const ContinuationRequest& request, TDone done) = 0;   // false: nothing to ask with
    virtual void trackEvent(TrackEvent event, const Track& track, double playedSeconds) = 0;
};
using THistoryWaveFactory = std::function<std::shared_ptr<Continuation>()>;
```

What a queue goes on with after its head
([spec/player/continuation.md](../../spec/player/continuation.md)):
- `begin(true)` comes once the queue has taken the source, before its first track starts; a vibe
  sends `radioStarted` there (TRK-03).
- `more` is asked for the next tracks when the queue nears its end. It calls `done` once, later or
  at once, with the tracks (none when there are none, `failure` an English text when the request
  failed); or it returns `false` and never calls `done`.
- `trackEvent` hears the events of the tracks it gave, and only those, also after the queue moved
  on to another source.
- `jamSeeds` in a request are the jam server's seeds while a jam is on, and `seedsVersion` in a
  reply says that the tracks come from a session started on those seeds of that version; see
  [The layer](#the-layer).
- A source makes the continuation for its own tracks: Yandex's vibe is
  [`YandexSource::Vibe`](../sources/yandex/README.md), its vibe from history
  `YandexSource::HistoryWave` (through `THistoryWaveFactory`, one per queue).

## `queue_source.h`: `QueueSource`, `QueueEntry`

```cpp
struct QueueSource {
    QString title;
    QList<Track> head;                        // the fixed tracks; may be empty
    std::shared_ptr<Continuation> continuation;   // may be null
    QList<Track> given;                       // the continuation's tracks so far (a vibe's first batch)
    bool waveFromHistory = true;              // false: never a vibe from history (CONT-12)
};
enum class EntryPart { Head, Continuation, Item, Other };
struct QueueEntry {
    Track track;
    EntryPart part;
    std::shared_ptr<Continuation> giver;      // a Continuation entry's
    bool fromHistory;                         // the vibe from history gave it
    std::optional<int> seedsVersion;          // from ContinuationReply: a session on the jam's seeds
    QString itemId, addedBy;                  // an Item's while its layer is on
    bool pending;                             // an Item that has not been current yet
};
struct LayerItem { QString itemId; Track track; QString addedBy; };
struct QueueRules { bool playReports = true; bool previousRestartsOnly = false;
                    bool ownContinuationFeedback = true; bool historyFeedback = true; };
struct QueueLayer { QueueRules rules; std::function<void(const QStringList& itemIds)> removeItems; };
```

`Player::entries()` lists the queue as entries: a head track; a continuation's track with the
continuation that gave it; a layer's item; or `Other`, a track kept from an earlier base that plays
only when picked. The members have default initializers, so a source is written with designated
initializers (`QueueSource{.title = …, .head = …}`).

## `track_navigator.h`: `TrackNavigator`

```cpp
enum class ShuffleAlgorithm { Random, WithoutRepeats };
inline constexpr int kEndlessTracks = std::numeric_limits<int>::max();
class TrackNavigator {
    virtual void reset(int size, int current) = 0;
    virtual void select(int index) = 0;
    virtual int next(bool repeat) const = 0;
    virtual int previous(bool repeat) const = 0;
    virtual int remaining(bool repeat) const = 0;     // still to come after the current; kEndlessTracks
};
std::unique_ptr<TrackNavigator> MakeTrackNavigator(std::optional<ShuffleAlgorithm> algorithm);
```

No algorithm means sequential traversal. The three implementations are private to the `.cpp`;
Player knows only the interface. Navigation is independent of audio, networking, settings and UI.
`reset` takes a queue size and its valid current index, or -1 for before the first entry: then
`next` gives the first one to play. `select` takes an existing queue index. `next` and `previous`
return queue indices, or -1 when no entry follows. Repeated lookahead returns the same candidate
and does not advance. Empty queues return -1. `remaining` is endless with repeat, and for Random
with two or more entries.

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
using JamEntry = LayerItem;
struct JamPlayback { enum class Kind { Item, Base, Idle }; Kind kind; QString itemId;
                     std::optional<Track> track; qint64 positionMs; bool paused; QUrl link, nextLink; };
inline constexpr int kJamPlayingReportMs = 10'000;
bool IsJamItem(const QueueEntry* entry);             // an Item with an item id: one of this jam's

class JamMode : public QObject {
public:
    explicit JamMode(Player* player, QObject* parent = nullptr);
    bool isActive() const;
    void start(bool waveFeedback);
    void setQueue(const QList<JamEntry>& entries, const QStringList& seeds, int seedsVersion);  // each state
    void skip(const QString& itemId);                 // command{skip}
    void end();
    void reportPlayback();                            // now, as after `resumed` (HOST-26)
    void setPlayingReportInterval(int ms);            // for tests
Q_SIGNALS:
    void itemStarted(const QString& itemId);          // -> started{itemId}
    void playback(const Core::JamPlayback& playback); // -> playing
    void removeRequested(const QStringList& itemIds); // the host removed waiting items -> remove
};
```

The scenarios are [spec/jam/host.md](../../spec/jam/host.md) with Desktop = Kickoman/QiYaa#13 and
#28; `tests/jam_mode_test.cpp` checks them by ID. The network, the protocol and the server's
messages are the host session's (Kickoman/QiYaa#14); `JamMode` gets the queue as `JamEntry`s and
reports through its signals. What the queue does is the `Player`'s [layer](#the-layer).

- **The queue** is the current track, then the **jam part** (the layer's waiting items, the mirror
  of the server's queue), then the **base**: what the host's own source still has to play, the
  rest of its head and then its continuation, with the queue's flag, shuffle and repeat. With no
  continuation of its own and the flag on, the base goes on with the vibe from history, which is
  the **jam vibe** while the jam is on.
- **Start** (HOST-14): `Player::setLayer` with `QueueRules{playReports = false,
  previousRestartsOnly = true, ownContinuationFeedback = false, historyFeedback = waveFeedback}`
  (HOST-15, HOST-16, HOST-07). The queue stays: the track that plays goes on and is reported as a
  base track, and an empty queue is an empty base. `itemStarted` and the reports follow
  `Player::trackEvent`.
- **The jam part** (HOST-01 to HOST-04, HOST-12): `setQueue` hands the state to
  `Player::setLayerItems` with the seeds. When nothing played before it (Empty, waiting for more,
  or Stopped with nothing next), `Player::next()` then starts the first item (HOST-08).
- **The base** (HOST-10 to HOST-13) plays when no item waits, by the ordinary rules; a base track
  is reported as `Base` (the protocol's `source: wave`) and never gets `started`. The jam vibe takes
  the last state's seeds, or the queue's history without any (HOST-11). When the base has nothing
  more, the player stops and reports `Idle` (HOST-13), and the next item starts at once (HOST-08).
- **A pick during the jam** (HOST-21) is `Player::setSource` under the layer: it becomes the new
  base, and nothing starts. `YandexSource::Catalog` does nothing of its own about the jam.
- **Editing the playlist** (HOST-36): `Player::removeRows` asks the layer to remove waiting items,
  which `JamMode` passes on as `removeRequested`; the host session sends `remove` for each.
- **Not learning** (HOST-15, HOST-16): no play reports; no feedback to the base's own vibe; rotor
  feedback only for the jam vibe's tracks, and only when `start` got `waveFeedback`. The route of a
  track's events is taken at its start, so a track that started before the jam closes as it began.
- **Listening along** (`spec/jam/listen.md`). `JamPlayback::link` is `Player::currentLink()`: the
  file of the current track once its link resolved, including a preloaded track that took over;
  `JamPlayback::nextLink` is `Player::nextLink()`, the preloaded next file. The host session sends
  them to the jam's guests when the host allows it.
- **Reports**: `itemStarted(itemId)` when a jam item's `Started` fires; `playback` after every
  track event, on the `Player`'s `stateChanged`, seeks, and every 10 s while `state()` is Playing
  (HOST-05, HOST-06). `Idle` when there is no current track or nothing plays: Empty, waiting for
  more, or Stopped with nothing next; otherwise the current track, as `Item` or `Base`.
- **Skip** (HOST-18, HOST-19): `skip(itemId)` is `Player::next()` only when that item is current.
- **End** (HOST-32, HOST-33): `Player::setLayer(std::nullopt)`. The current track plays on, the
  waiting items play first as ordinary tracks, the jam vibe's tracks on the server's seeds go, and
  the vibe from history starts a new session from the queue's history when it is asked next.

**Traps:**
- The layer's `removeItems` holds a `QPointer<JamMode>`; a removal after the `JamMode` is gone
  does nothing.
- HOST-09 (a broken last jam track) is the `Player`'s own ERR-07: `DecideOnFailure` says Next when
  a continuation follows (the policy table in `tests/failures_test.cpp`). `jam_mode_test` does not
  repeat it.

## `lyrics.h`: `Lyrics`

```cpp
inline constexpr qsizetype kMaxLyricsBytes = 256 * 1024;      // a text over it, in UTF-8, is not taken
inline constexpr qsizetype kMaxLyricsLines = 2000;
inline constexpr qsizetype kMaxLyricsLineCharacters = 500;

struct LyricsLine { qint64 ms = -1; QString text; };            // ms < 0: a line of plain text
enum class LyricsKind { Synced, Plain };
struct Lyrics { LyricsKind kind = LyricsKind::Plain; QList<LyricsLine> lines; };
enum class LyricsFrom { Catalog, LrcFile, FileTags, ThirdParty };   // what the view names as the text's origin
struct LyricsAnswer {
    enum class Status { Found, Missing, Instrumental, Failed, NoAccount };
    Status status = Status::Missing;
    Lyrics lyrics;                                               // Found only
    LyricsFrom from = LyricsFrom::Catalog;
    bool serverError = false;                                    // Missing after the server kept failing
};
using TLyricsDone = std::function<void(LyricsAnswer answer)>;

std::optional<Lyrics> ParseLyricsText(QStringView text);        // LRC or plain; nullopt: nothing, or over the limit
std::optional<Lyrics> PlainLyrics(QStringView text);            // plain whatever the text holds
```

- `ParseLyricsText` follows `spec/lyrics/README.md` (LYR-11, LYR-12), and `lyrics_test` checks it
  against `spec/lyrics/lrc.json`. A text with one line that starts with a time tag is synced; its
  lines are sorted by time, a pause is an empty line, and lines without times are dropped. Any
  other text is plain, with runs of empty lines made one.
- `PlainLyrics` is the plain half alone, for a source whose text never carries times (Yandex
  Music's `fullLyrics`).
- `LyricsFrom::Catalog` is the catalogue of the track's source; `src/ui` words it by
  `track.key.source`. `LrcFile` and `FileTags` are a local file's. `ThirdParty` is the lyrics
  service's: `LyricsFinder` sets it on every answer of the service.
- `serverError` lets a service say "nothing found" while counting a failure: LRCLIB answers some
  misses with 503 ([src/lrclib](../lrclib/README.md)).

## `lyrics_finder.h`: `LyricsFinder`

```cpp
class LyricsService : public QObject {                           // the lyrics service, outside core
public:
    virtual void lyrics(const Track& track, TLyricsDone done) = 0;   // as TrackSource::lyrics
    virtual void cancel() = 0;                                   // the running request's done never comes
};

struct LyricsLookup {
    enum class Shown { SourceSynced, ThirdPartySynced, SourceText, ThirdPartyText, Instrumental, None };
    Shown shown = Shown::None;
    bool thirdPartyFailed = false;
};

class LyricsFinder : public QObject {
public:
    enum class Phase { Idle, Loading, Shown, Missing, Instrumental, Failed, NoAccount };
    struct View {
        std::optional<Track> track;                              // the track looked up; none when Idle
        Phase phase = Phase::Idle;
        Lyrics lyrics;                                           // Shown only
        LyricsFrom from = LyricsFrom::Catalog;
        bool better = false;                                     // a text higher in the order may still come
    };
    LyricsFinder(Player* player, SourceRegistry* sources, LyricsService* thirdParty, QObject* parent = nullptr);
    void setActive(bool on);                                     // the view is open
    void setThirdPartyEnabled(bool on);                          // off until set
    bool hasThirdParty() const;                                  // a service was given
    void retryFailed();                                          // the network is back
    void forget();                                               // the account changed
    void setWaits(int answerMs, int sourceSilentMs);             // 8 s and 1 s unless a test sets them
    const View& view() const;
Q_SIGNALS:
    void changed();                                              // view() was set again
    void lookedUp(const Core::LyricsLookup& lookup);
};
```

The rules are `spec/player/lyrics.md`'s; `tests/lyrics_test.cpp` checks them by ID.

- **Only while active** (LYR-01). Inactive, it asks nothing and its view is `Idle`. Turning it on
  looks up the current track, from memory when it can; turning it off drops what is being asked.
- **The current track.** It follows `Player::currentTrackChanged` and `playlistChanged` (the cursor
  can move without the first) and compares the `TrackKey`, so the many signals of one track ask
  once. No current track is `Idle`.
- **The order** (LYR-03, LYR-05). The source is asked at once. The service is asked when it is
  given and on, and the source has no synced text: it answered without one, or has not answered
  within the silent wait. A synced answer of the source cancels the service. The view shows the
  first text of: the source's synced, the service's synced, the source's plain, the service's
  plain. Without a text: `Instrumental` when the service says so, `Loading` while anyone is asked,
  then `NoAccount` or `Failed` by the source's answer, else `Missing`.
- **Waits.** A source or a service that has not answered within the answer wait has nothing, and
  its late answer is dropped; the service gets `cancel()`.
- **Stale answers** (LYR-09). Each request has a number and each `done` a `QPointer` to the
  finder: an answer for an earlier request, track or lookup, or after the finder is gone, does
  nothing.
- **Memory** (LYR-14). The answers of the last 10 tracks, the source's and the service's apart.
  `Failed`, `NoAccount` and a wait that ran out are not kept. An answer from memory sent no request,
  so its `serverError` does not count again. `forget()` empties it and looks up the
  current track again.
- **Turning the service on and off** (LYR-15). On: asked at once when the source has no synced
  text. Off: its answer is dropped from the view and its request cancelled.
- **`retryFailed`** (LYR-17) asks again whoever answered `Failed` for the current track.
- **`lookedUp`** comes once per lookup that sent a request, when everyone asked has answered, when
  the track changes, or when the finder turns off, with what the view showed then: the telemetry's
  lyrics counts (`spec/telemetry`). Turning the service on during a track starts a new lookup.
- **Threads.** Main thread only, like the `Player` it follows.

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
    QString localFile(const QUrl& url) const;  // empty until the file is on disk; a file: URL's own file

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
  then goes to the network. A `file:` URL is its own file on disk: it is read from there, never
  copied into the directory and never downloaded, so it works without a network manager:
  - A memory hit makes the URL the most recently used and returns the image.
  - If `localFile(url)` exists, holds at most `kMaxLocalFileBytes` (16 MiB) and `QImage` decodes
    it, the image goes into memory and is returned.
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
- On reading, a file that `QImage` cannot decode, or one over 16 MiB, is treated as missing.
- A `file:` URL is not kept here: its own file is read in place (the local source's covers,
  `<cache>/local-covers/`).

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
| continuation | a reply with `failure`: the notice `VibeFailed`; with no tracks a waiting `next()` stops (WAVE-09), and the vibe from history's first such reply stalls it (CONT-07) |
| seek | `seekTo` and `seekFraction` return `false` |
| index out of range | `playIndex` does nothing; `removeRows` skips it |
| cover | a null `QImage` from `get`, an empty `QString` from `localFile`, and no `ready` |

A continuation's calls, its `done` and the `trackEvent` signal's slots run inside Qt slots. Per
`CLAUDE.md`, no exception may cross the event loop, so they must not throw.

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
    (`generation`, `preloadGeneration`, the load-more generation) and the rules taken when a track
    starts.
  - `tests/library_test.cpp`: track events, the preload and the gapless advance, re-preloading
    after a queue change, the endless source, and a slow load against a newer choice.
  - `tests/audio_test.cpp`: `playerPollsTheEngine`.
  - `tests/windows_test.cpp`: `nothingPlaysAfterShutDown`.
  - `tests/mpris_test.cpp`: the cache without a network manager.
