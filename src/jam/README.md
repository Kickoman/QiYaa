# `src/jam` — the jam: protocol, connection, the host's session

The connection to a jam server (Kickoman/QiYaa-jam) by the protocol of `spec/jam/protocol`: the
message model, its JSON with the schema's checks, the WebSocket client with its handshake,
reconnects, clock offset and outbox, the file where the host keeps its jam between runs, and the
host's session that ties them to the queue and to Yandex. It does **not** decide what plays (the
jam mode of the queue is `Core::JamMode`) and has no windows (Kickoman/QiYaa#15). It reads no
`QSettings` and does not know where files live: `src/app` passes the server address and the
session file's path.

The module is optional: it is built only with Qt WebSockets (`QIYAA_HAVE_JAM`, see
[docs/building.md](../../docs/building.md#параметры-cmake)). It links Qt Core, Qt WebSockets,
`qiyaa_core` and `qiyaa_yandex`, and PRIVATE `qiyaa_sources_yandex`; only `host_session.*` uses
the last three.

```bash
grep -rln 'include "\(ui\|app\|audio\|skins\|vis\|integrations\)/' src/jam/   # must print nothing
grep -ln 'include "\(core\|yandex\|sources\)/' src/jam/* | grep -vx 'src/jam/host_session\.\(h\|cpp\)'   # must print nothing
```

| File | Contains |
|---|---|
| `protocol.h` | `ClientMessage`, `ServerMessage` and every message and model of the protocol: `Track`, `Room`, `Settings`, `SettingsPatch`… |
| `codec.h/.cpp` | `Encode`, `DecodeServer`, `DecodeClient`, `Decoded<T>`, `IsKnownReason`, `TypeOf` |
| `client.h/.cpp` | `Client`, `ClientOptions`, `Status` — one connection to the server at a time |
| `session_store.h/.cpp` | `Session`, `SessionStore`, `EncodeSession`, `DecodeSession` |
| `host_session.h/.cpp` | `HostSession`, `HostOptions`, `HostConfig`, `HostPhase`, `HostEnd`, `JamTrackOf`, `BaseTrackOf`, `CoreTrackOf` |

## `protocol.h`

The structs follow `spec/jam/protocol/schemas` field by field, one struct per message `type`, and
compare with `==`. Names that the protocol uses for something else in C++: `settings` is
`ChangeSettings`, `searchResult` is `SearchResult`, `snapshot` is `Snapshot`, `state` is `State`.

- An optional string field (`Track::albumId`, `coverUri`, `NowPlaying::itemId`, `Rejected::id`…) is
  an empty `QString` when absent; optional objects and enums are `std::optional`.
- `Track::coverUri` is Yandex's template with `%%` for the size and no scheme, the same as
  `Yandex::Track::coverUri`.
- `Track::source` is `TrackSource::Yandex` unless the JSON says `host`: a track that only the host
  can play, a local file for one (HOST-37), with no `albumId` or `coverUri`. Such a track is valid
  only in `playing`, `nowPlaying` and `add` (which the server refuses, ROOM-66); queue and recent
  items, search and check results are tracks of the catalog (`CatalogTrack` in the schemas).
- `Resume::outbox` and `Session::outbox` hold the item ids of `started` events in order; the codec
  writes them as `{"type": "started", "itemId": …}`.
- `Resume::snapshot` and `Snapshot::data` are `QJsonObject`: the host stores the snapshot and sends
  it back without reading it.

## `codec.h`

```cpp
template <typename T> struct Decoded { std::optional<T> message; QString problem; bool unknownType; bool isValid() const; };
QByteArray Encode(const ClientMessage& message);            // compact JSON
Decoded<ServerMessage> DecodeServer(QByteArrayView text);
Decoded<ClientMessage> DecodeClient(QByteArrayView text);   // what the server would accept
bool IsKnownReason(const QString& reason);
QString TypeOf(const ClientMessage&);  QString TypeOf(const ServerMessage&);
```

Decoding checks what the schemas check: required fields and their JSON types, enum values, the
patterns of ids and secrets, lengths of names, titles and texts (in code points), limits of
numbers and lists, "exactly one of" (`add`, `searchResult`, `validateResult` entries), the
conditional fields of `playing` and `nowPlaying` (no listen links beside a track only the host
can play, LISTEN-14), tracks of the catalog where the schemas say so, an outbox of `started`
only, the snapshot's `format` 1, and **no field named like a secret** at any depth of a `state`'s
room or a snapshot. Unknown fields are ignored. `problem` names the first failure with its place
(`state.room.queue itemId does not match …`).

- An unknown `type` gives `unknownType` and no message: the client skips it.
- A `rejected` with a reason the app does not know is not valid (`problem` says so), but its
  `message` is kept: the protocol says to show a general failure for it.
- `DecodeClient` exists for the tests and for `Client::send`, which never sends what it would
  refuse.

The numbers are a copy of `spec/jam/protocol/schemas/defs.schema.json` and `spec/jam/limits.md`.
The patterns are copied as the schemas write them and read as Ajv reads them, as ECMAScript with
the `u` flag: over the whole text, with `\s` as ECMAScript's white space (U+00A0 and U+FEFF
included, U+0085 not). `jam_test` decodes every file of `spec/jam/protocol/examples`: the valid
ones must pass and the `invalid-*` ones must fail, so a change in the spec that the codec does not
follow fails a test.

**Checked by hand, not by the schemas** (#32). Checking by the schemas with a library was tried
with valijson: on 50,000 messages it agreed with the server's Ajv, but it needed two patches of its
own and a translation of the patterns anyway, and added about 400 KB to the app. The schemas change
seldom, and the examples catch a codec that does not follow them.

**Traps:**
- `QRegularExpression` (PCRE2) reads a pattern differently from ECMAScript: its `$` also matches
  before a final line feed, and its `\s` is ASCII only, or, with `UseUnicodePropertiesOption`,
  misses U+FEFF and takes U+0085. So `Pattern()` matches the whole text (`anchoredPattern`) and
  writes `\s` and `\S` out as ECMAScript's white space (`kSpace`). `\S` inside a class would need
  more; no schema has one.
- Where a schema sets no upper limit, the codec stops where its field ends: `protocol`,
  `serverProtocol` and `seedsVersion` at `INT_MAX`, `pending` at the queue's 300, `version`,
  positions and times at 2^53 − 1. The server never sends more; Ajv would take it.
- A number must be whole: JSON has no integer type, so `2.5` for `positionMs` reads as a number and
  is refused here. `Reader::integer` checks the range before `static_cast<qint64>`.

## `client.h`

```cpp
enum class Status { Idle, Connecting, Online, Offline, Stopped };
struct ClientOptions { QString appVersion; std::vector<int> reconnectDelaysMs = {1000, 2000, 4000, 8000, 16000, 30000};
                       int pingIntervalMs = 20000; std::function<qint64()> clock; };
class Client : public QObject {
    Status status() const;  qint64 clockOffsetMs() const;  qint64 serverNow() const;  const QStringList& outbox() const;
    void start(const QUrl& url);  void stop();
    bool send(const ClientMessage& message);          // false: not online, or the message would be refused
    void started(const QString& itemId, bool sendNow = true);
    void restoreOutbox(const QStringList& itemIds);  void clearOutbox();
    void networkBack();                               // reconnect at once when Offline
    static QUrl SocketUrl(const QString& serverUrl);  // https://jam.example.org -> wss://jam.example.org/ws
Q_SIGNALS:
    void statusChanged(Jam::Status);  void welcomed();  void messageReceived(const Jam::ServerMessage&);
    void outboxChanged(const QStringList&);
};
```

- On open the client sends `hello{protocol: 1, app: desktop, appVersion}`. The version keeps only
  what the schema allows (`[0-9A-Za-z.+_-]`, 32 at most; `unknown` when nothing is left): the
  server refuses any other `hello`, and bans an address that keeps sending one. `welcome` makes it
  `Online`, sets the clock offset (server time − `clock()`) and emits `welcomed()`: the owner then
  sends `create`, `resume` or `join`. Every `state` updates the offset too.
- A close or an error of the socket makes it `Offline` and schedules a new connection after
  1, 2, 4, 8, 16, 30, 30 … s (`reconnectDelaysMs`); `welcome` starts the count over.
  `networkBack()` (in the app, from `QNetworkInformation`) reconnects at once.
- The client pings every `pingIntervalMs` and drops a connection that did not answer the previous
  ping: a dead network then shows as `Offline` in seconds, not when TCP gives up. The pings start
  with the attempt, not at `connected`: a connection that hangs in TCP, TLS or the upgrade is
  dropped after two intervals too, and this is the only timeout it has.
- After `ended`, `kicked` and `rejected{update-required}` the message is emitted first, then the
  client stops: `Stopped`, no more reconnects.
- A message that does not decode is skipped with a warning; an unknown `type` silently.
- `send` refuses (returns false) without a connection that got `welcome`, and refuses a message
  that `DecodeClient` would not accept: the server would close the connection over it, and a
  reconnect would send it again.
- The outbox only collects: `started(itemId, sendNow)` sends at once, or keeps the item id when it
  cannot or when `sendNow` is false (before the server accepted `create` or `resume`, HOST-25). An
  item id already waiting is not added again.
  The owner sends the outbox in `resume` and calls `clearOutbox()` after `resumed`.
- No `Origin` header is sent; the server accepts apps without one.

**Traps:**
- Each connection is a new `QWebSocket`; a signal of an older one is ignored. Do not keep a pointer
  to the socket outside the client.
- `messageReceived` is emitted synchronously from the socket's signal: a slot that calls `stop()`
  or `start()` is fine, but must not delete the client.
- Never `close()` a `QWebSocket` that is already `UnconnectedState`, as in its own
  `stateChanged`. After `abort()` over TLS, `QSslSocket` has dropped its write buffer but is still
  open while it tells of the disconnect, and the close frame is written into nothing: SIGSEGV in
  `QRingBuffer` (#38). Plain `ws://` only warns, so only a `wss://` test catches it.

## `session_store.h`

What the host keeps while its jam lasts (HOST-22): `roomId`, `hostSecret`, `joinUrl`, the last
snapshot and the outbox. The file is compact JSON, the same format as the Android app's:

```json
{"version":1,"roomId":"7k3m9q2x","hostSecret":"…","joinUrl":"https://…/j/7k3m9q2x#…","snapshot":{"format":1,"room":{…}},"outbox":["i4"]}
```

`save` writes through `QSaveFile` (a temporary file and a rename) and creates the folder; it
returns false and logs when the file cannot be written. `load` gives `nullopt` for a missing,
broken or foreign file (another `version`, a required field missing) and for one over
`kMaxSessionFileBytes` (4 MiB) before reading it. `clear` deletes it.

## `host_session.h`

```cpp
struct HostConfig { QString serverUrl; bool waveFeedback = true; bool shareAudio = false; };
struct HostOptions { ClientOptions client; std::function<HostConfig()> config;
                     std::function<QString()> newId; int resumeRetryMs = 30'000; };
enum class HostPhase { None, Creating, Active };
enum class HostEnd { ByHost, ByServer, Expired, Gone };
std::optional<Track> JamTrackOf(const Yandex::Track& track);   // nullopt: the protocol cannot carry it
std::optional<Track> JamTrackOf(const Core::Track& track);     // nullopt also for another source's track
std::optional<Track> BaseTrackOf(const Core::Track& track, const QString& hostSecret);  // any source
Core::Track CoreTrackOf(const Track& track);                   // a Yandex track: {"yandex", "<id>:<albumId>"}
class HostSession : public QObject {
    HostSession(Core::JamMode*, Yandex::Library*, SessionStore, HostOptions, QObject* parent = nullptr);
    HostPhase phase() const;  Status connection() const;  bool isConnected() const;
    const std::optional<Room>& room() const;  QString joinUrl() const;  bool hasStoredSession() const;
    QString participantName(const QString& publicId) const;   // empty for one the room does not list
    bool create(const QString& hostName, const std::optional<SettingsPatch>& settings = {});
    void cancelCreate();  void continueStored();  void discardStored();  void end();
    bool canAdd(const Core::Track&) const;              // connected, and a track of the catalog
    bool add(const Core::Track&);  bool playNext(const Core::Track&);  bool pin(const QString& itemId);
    bool remove(const QString& itemId);  bool kick(const QString& publicId);
    bool changeSettings(const SettingsPatch&);  bool rotateLink();
    void networkBack();
Q_SIGNALS:
    void changed();  void refused(const QString& reason);  void ended(Jam::HostEnd why);
    void addRefused(const QString& trackId, const QString& reason);   // which add: the track's catalog id
    void activeChanged(bool active);                  // edges of phase() == Active
    void connectedChanged(bool connected);            // edges of the connection while the jam is on
    void offline();                                   // the host removed jam items without a connection
};
```

The host's side of `spec/jam/host.md`, a port of the Android app's `JamHost`: the `Client` to the
server, `Core::JamMode` for the queue, `Yandex::Library` for the guests' search and checks, the
`SessionStore` for the jam between runs.

`addRefused` comes before `refused` when the request the server rejected was an `add` (also the add
of `playNext`): the session keeps each add's id with its track until a state lists the track, the
server rejects it or the session ends. The search window shows the refusal on that track's row
(HOST-38); `refused` is still the status for every rejection.

- **Create.** `create` trims the name and cuts it to 24 characters; it returns false while a jam is
  on or being created, without a server address (`config().serverUrl`), and for a name or settings
  the server would refuse. After `welcome` it sends `create`; `created` stores the session and
  starts `JamMode` with `config().waveFeedback`. A refused `create` (`rate-limited`, `server-full`…)
  is `refused(reason)` and no jam.
- **Resume.** Every `welcome` of an active jam sends `resume` with the stored snapshot and the
  client's outbox (the last 500). After `resumed` the outbox is cleared and `JamMode` reports what
  plays (HOST-26). `room-not-found` or `bad-secret` ends the jam here (`ended(Gone)`, HOST-24);
  another refusal is `refused` and a new try after `resumeRetryMs`.
- **"Continue the jam?"** (HOST-23). The constructor loads the stored session; `hasStoredSession()`
  asks the app to put the question. `continueStored()` starts `JamMode` at once (the queue is not
  kept between runs: the base is what the queue holds then, and the jam part comes with the first
  `state`) and connects. `discardStored()`
  clears the file at once, then connects only to `resume` and `end` the room; the guests see the
  end.
- **State.** A `state` older than the last one of the same connection is ignored; the first after
  a `welcome` is taken whatever its version. It goes to `JamMode::setQueue`, and to `room()`.
- **The queue's events.** `JamMode::itemStarted` → `Client::started` (to the outbox while not
  connected, HOST-25); `JamMode::playback` → `playing`, only while connected, a base track as
  `source: wave` with `BaseTrackOf` and the session's `hostSecret` (HOST-10, HOST-37). A base track
  that gives nullopt is not reported, and the guests go on seeing the last one.
  `JamMode::removeRequested` → `remove` for each item; without a connection that fails, and
  `offline()` lets the app say so (HOST-25, HOST-36).
- **Names.** `participantName(publicId)` is the name of a participant of `room()`, for the line
  that says who added an item (HOST-34).
- **Guests.** `searchRequest` → `Library::searchTracks` (`type=track`), the available tracks that
  `JamTrackOf` takes, 20 at most; a 401 or 403 is `error: unauthorized`, any other failure `failed`
  (HOST-28, HOST-29). `validateRequest` → `Library::tracksByIds`: an entry per id in order, a track
  or `track-unavailable`; a failed request fails them all (HOST-30). Answers go only while
  connected.
- **The host's actions** (HOST-20) return false without a connection. `add` and `playNext` take
  only a track of the catalog (`JamTrackOf`) and return false for any other: a track only the host
  can play is not added (HOST-37, ROOM-66), and the codec would let such an `add` through, since
  the refusal is the server's. `canAdd` says beforehand whether they can work, so a window disables
  the action instead of failing it. `playNext` pins a track already waiting; otherwise it adds it
  and pins the item when a `state` brings it (added by the host), unless it came pinned. A refused
  add of `playNext` is `refused` and nothing is pinned.
- **End.** `end()` sends `end` when connected, then ends here: the file is cleared, `JamMode::end`
  keeps the waiting jam items as ordinary tracks that play first (HOST-32), the client stops,
  `ended(ByHost)`. `ended` from the server does the same without `end` (`ByServer`, `Expired`).
- **Listening along** (`spec/jam/listen.md`, LISTEN-01 to LISTEN-05). With `config().shareAudio`,
  a `playing` for an item or a base track carries `listenUrl`, the file the host plays
  (`JamPlayback::link`), and `listenNextUrl`, the preloaded next one (`JamPlayback::nextLink`),
  each when it is a file of Yandex Music's storage (`IsListenUrl`); any other link (a test's mock
  server) is left out, so the server takes the report. Guests on the jam page may then play it.
  While a track only the host can play is current, `playing` carries neither link, whatever the
  player has: the codec would drop the whole message (LISTEN-14). For such a track next,
  `IsListenUrl` is the whole rule: only the Yandex source gives files of Yandex's storage, so the
  next file of another source (a path, a test's `https://fake.example/…`) never passes.
- `changed()` follows any change of `phase()`, the connection, `room()` or `joinUrl()`.
- **The jam on and its connection** (HOST-25, HOST-26), for the app's status and telemetry:
  - `activeChanged` gives the edges of `phase() == Active`: `true` after `created` and at once in
    `continueStored()`, `false` when the jam ends;
  - `connectedChanged` gives the edges of `isConnected()` while the jam is on: `true` after
    `created` and after every `resumed` (the first one too), `false` when an active jam loses
    its connection; the end of the jam says nothing about the connection;
  - a refused `create` and `discardStored()` send neither.

`JamTrackOf` builds the protocol's track like the server checks it: a catalog id
(`[0-9A-Za-z_-]{1,64}`, else nullopt), the album id only when it is one, control characters as
spaces, the title cut to 150 characters (nullopt when empty), up to 10 non-empty artists of 64
characters, the duration within 0 … 24 h, and the cover only as a template with `%%`, no scheme
and no spaces, 300 characters at most. Cutting never splits a surrogate pair.

A track of the catalog is one of the Yandex source (`YandexSource::YandexTrackOf`, by
`TrackKey::source`); any other source's track is one that only the host can play. Jam items, the
guests' search and checks, `add` and `playNext` take only the catalog: `JamTrackOf(Core::Track)`
is nullopt for another source's track. `BaseTrackOf` is for what the host reports as its base
(HOST-37):
- a track of the catalog is `JamTrackOf`'s, and nullopt stays nullopt: a Yandex track the protocol
  cannot carry is never turned into the host's own;
- any other is `{id, source: Host, title, artists, durationMs}` under `JamTrackOf`'s limits, with
  no `albumId` or `coverUri`. The `id` is the first 16 hex digits of HMAC-SHA256 with the
  `hostSecret` as the key over `source + "\n" + id` of its `TrackKey`: the same for that track all
  the jam session (the `hostSecret` survives a new link and a resume, HOST-22), another in another
  room, and nothing a guest could tell a path or a file name by;
- a title that is empty after the cleaning gives nullopt, and the track is not reported. A source
  of such tracks must always give one (a local file: its name without the extension).

`CoreTrackOf` is how a room's items reach `Core::JamMode`: the key from
`YandexSource::KeyOf(id, albumId)`, the title, artists, duration and cover, `available`. `playNext`
looks for the track in the room by its catalog id (`YandexSource::CatalogId`), since a room's items
carry the track id alone.

**Traps:**
- The search's and the check's callbacks may come after the session is gone or the connection
  changed: they hold a `QPointer` and send only while connected.
- `request` takes a builder, so a refused action does not use up a request id.

## Not here

- What the server does and the protocol's rules: Kickoman/QiYaa-jam and `spec/jam/`.
- The jam over the queue (the mirror of the state, the base, the jam vibe, reports):
  `Core::JamMode`, [src/core/README.md](../core/README.md).
- Where the settings and the session file live, the question at start and the texts of refusals:
  [src/app/README.md](../app/README.md).
