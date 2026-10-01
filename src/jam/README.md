# `src/jam` — the jam client: protocol, connection, the host's stored session

The connection to a jam server (Kickoman/QiYaa-jam) by the protocol of `spec/jam/protocol`: the
message model, its JSON with the schema's checks, the WebSocket client with its handshake,
reconnects, clock offset and outbox, and the file where the host keeps its jam between runs. It
does **not** decide what plays (the jam mode of `Core::Player` and `Core::Sources`, Kickoman/QiYaa#13),
does not talk to Yandex or the player (the host session, Kickoman/QiYaa#14) and has no windows
(Kickoman/QiYaa#15). It reads no `QSettings` and does not know where files live: `src/app` passes
the server address and the session file's path.

The module is optional: it is built only with Qt WebSockets (`QIYAA_HAVE_JAM`, see
[docs/building.md](../../docs/building.md#параметры-cmake)). It links Qt Core and Qt WebSockets
and no other QiYaa module.

```bash
grep -rln 'include "\(core\|ui\|app\|yandex\|audio\)/' src/jam/   # must print nothing
```

| File | Contains |
|---|---|
| `protocol.h` | `ClientMessage`, `ServerMessage` and every message and model of the protocol: `Track`, `Room`, `Settings`, `SettingsPatch`… |
| `codec.h/.cpp` | `Encode`, `DecodeServer`, `DecodeClient`, `Decoded<T>`, `IsKnownReason`, `TypeOf` |
| `client.h/.cpp` | `Client`, `ClientOptions`, `Status` — one connection to the server at a time |
| `session_store.h/.cpp` | `Session`, `SessionStore`, `EncodeSession`, `DecodeSession` |

## `protocol.h`

The structs follow `spec/jam/protocol/schemas` field by field, one struct per message `type`, and
compare with `==`. Names that the protocol uses for something else in C++: `settings` is
`ChangeSettings`, `searchResult` is `SearchResult`, `snapshot` is `Snapshot`, `state` is `State`.

- An optional string field (`Track::albumId`, `coverUri`, `NowPlaying::itemId`, `Rejected::id`…) is
  an empty `QString` when absent; optional objects and enums are `std::optional`.
- `Track::coverUri` is Yandex's template with `%%` for the size and no scheme, the same as
  `Yandex::Track::coverUri`.
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
conditional fields of `playing` and `nowPlaying`, an outbox of `started` only, the snapshot's
`format` 1, and **no field named like a secret** at any depth of a `state`'s room or a snapshot.
Unknown fields are ignored. `problem` names the first failure with its place (`state.room.queue
itemId does not match …`).

- An unknown `type` gives `unknownType` and no message: the client skips it.
- A `rejected` with a reason the app does not know is not valid (`problem` says so), but its
  `message` is kept: the protocol says to show a general failure for it.
- `DecodeClient` exists for the tests and for `Client::send`, which never sends what it would
  refuse.

The numbers are a copy of `spec/jam/protocol/schemas/defs.schema.json` and `spec/jam/limits.md`.
`jam_test` decodes every file of `spec/jam/protocol/examples`: the valid ones must pass and the
`invalid-*` ones must fail, so a change in the spec that the codec does not follow fails a test.

**Traps:**
- `\s` in these `QRegularExpression`s is ASCII whitespace only (no `UseUnicodePropertiesOption`),
  as in the Android codec, while Ajv on the server also counts Unicode spaces. A name that ends
  in a non-breaking space passes here and is refused by the server.
- A number must be whole: JSON has no integer type, so `2.5` for `positionMs` reads as a number and
  is refused here.

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

- On open the client sends `hello{protocol: 1, app: desktop, appVersion}`. `welcome` makes it
  `Online`, sets the clock offset (server time − `clock()`) and emits `welcomed()`: the owner then
  sends `create`, `resume` or `join`. Every `state` updates the offset too.
- A close or an error of the socket makes it `Offline` and schedules a new connection after
  1, 2, 4, 8, 16, 30, 30 … s (`reconnectDelaysMs`); `welcome` starts the count over.
  `networkBack()` (in the app, from `QNetworkInformation`) reconnects at once.
- The client pings every `pingIntervalMs` and drops a connection that did not answer the previous
  ping: a dead network then shows as `Offline` in seconds, not when TCP gives up.
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

## Not here

- What the server does and the protocol's rules: Kickoman/QiYaa-jam and `spec/jam/`.
- The host's behaviour (mirror of the queue, the jam wave, search for guests): `spec/jam/host.md`;
  in the app from Kickoman/QiYaa#13 and #14.
