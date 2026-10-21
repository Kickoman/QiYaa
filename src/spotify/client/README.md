# `src/spotify/client` — Spotify's client on Qt

Everything of Spotify that needs the network, on Qt Core and Network, around the sans-IO
[protocol](../protocol/README.md): the logins, the access point connection and its audio keys, the
tokens and requests of Spotify's web side, and a track's file from the CDN
([spec/player/spotify.md](../../../spec/player/spotify.md)). It does not know the core's tracks,
queue or failures: [src/sources/spotify](../../sources/README.md) turns it into a music source.
Namespace `SpotifyClient`, library `qiyaa_spotify_client`.

| File | What it is |
|---|---|
| `endpoints.h` | `Endpoints`: the hosts it talks to, replaced by tests |
| `identity.h/.cpp` | `kClientId`, `NewDeviceId`, `Identity`, `ClientTokenParams`, `UserAgent`: what QiYaa tells Spotify about itself on this OS and CPU ([the table](../protocol/README.md#what-qiyaa-tells-spotify-about-itself)) |
| `credentials_file.h/.cpp` | `LoadCredentials`, `SaveCredentials`, `ForgetCredentials`: the reusable credentials on disk |
| `login.h/.cpp` | `PairLogin` (a code at spotify.com/pair, SPOT-01) and `BrowserLogin` (PKCE with a redirect to `127.0.0.1`, SPOT-02): both end in an OAuth token |
| `session.h/.cpp` | `Session`: the access point connection over `QTcpSocket`: apresolve, the login, the keep-alive timer, reconnecting, audio keys with their retry |
| `web.h/.cpp` | `Web`: client-token and login5 tokens, kept until they expire; requests to `spclient` with them |
| `file_download.h/.cpp` | `FileDownload`: a track's Ogg Vorbis file: metadata, the file for a bitrate, its key, its CDN links, then the bytes, decrypted, without Spotify's 167-byte header |

Everything runs on the thread that made it, the main thread in the app; nothing blocks.

## Dependencies

- `qiyaa_spotify_client` links PUBLIC `qiyaa_spotify_protocol`, `Qt6::Core` and `Qt6::Network`.
- It includes only `spotify/` and Qt Core and Network: no Qt Gui (a browser login asks its caller to
  open the page), no other module.

```bash
grep -rnE --include='*.h' --include='*.cpp' '#include "(app|audio|core|integrations|jam|skins|sources|ui|vis|yandex)/' src/spotify/client/   # must print nothing
grep -rnE --include='*.h' --include='*.cpp' '#include <Q(Gui|Widget|Desktop|Image|Pixmap|Application)' src/spotify/client/                  # must print nothing
grep -rnw --include='*.h' --include='*.cpp' 'throw\|catch' src/spotify/client/                                                             # must print nothing
```

## Logins

- `PairLogin::start()`: RFC 8628 with the desktop client ID and the scope `streaming`. `codeReady`
  carries the code and the page with it (`verification_uri_complete`). It polls at the server's
  `interval`, 5 s longer after `slow_down`, until `succeeded(token)` or `failed(text)`. `cancel()`
  and the destructor end it silently.
- `BrowserLogin::start()`: listens on a free port of `127.0.0.1` and emits `openUrl` with the
  authorize page (PKCE S256, a random `state`): the caller opens it in the browser. The redirect gets
  a small page that says the browser can be closed; its code is exchanged for `succeeded(token)`. A
  redirect with another `state` or an `error` is `failed`. Only `GET /login` counts: anything else
  the browser asks for (a favicon) gets 404 and the login keeps waiting.
- A token is only good for one access point login (`Session::login` with
  `Credentials::FromAccessToken`): what to keep are the reusable credentials the session has after
  it.

## `Session`

```cpp
void login(const SpotifyProtocol::Credentials& credentials);   // a token or saved credentials
void logout();
bool loggedIn() const;
const SpotifyProtocol::Credentials& credentials() const;      // the reusable ones once logged in
QString username(), country(), product();
QStringList spclients() const;                                // from apresolve
void audioKey(const TGid& track, const TFileId& file, TKeyDone done);
Q_SIGNALS: loggedInChanged(bool), loginRefused(int code, QString), accountChanged()
```

- `login` asks apresolve for the access points and the spclient hosts (falling back to
  `ap.spotify.com:443` and `spclient.wg.spotify.com:443`), then tries the access points in order.
  `TryAnotherAp` moves to the next one.
- After a login it keeps the reusable credentials of `APWelcome` and logs in with them from then on.
  When the connection drops, the keep-alive fails or every access point refused the connection, it
  logs in again after 2, 4, 8, 16, 32 s, then every 60 s (SPOT-18, as ERR-02), from the first access
  point; when all of them fail, it asks apresolve again.
- A refused login (`loginRefused`, the protocol's `LoginError`) ends it: no retry. The caller
  decides what the code means (SPOT-04, SPOT-05).
- `audioKey` answers once: the key, or `KeyFailure`. `Refused` is code 1 (SPOT-17). Code 2 or no
  answer in `kKeyTimeoutMs` is asked once more after `kKeyRetryMs`, then `Failed` (SPOT-16). A
  request made before the login waits for it, at most `kQueuedKeyTimeoutMs`; one in flight when the
  connection drops, and every one after `logout`, is `Network`.

## `Web`

`spclient(WebRequest, done)` sends to the first spclient host with `Authorization: Bearer <login5>`,
`client-token`, QiYaa's `User-Agent`, the request's `Content-Type` and `Accept` (protobuf by default) and, unless
`accountQuery` is false, `product=0&country=<cc>&salt=<random>` added to the query (context-resolve takes it
without). `pathfinder(query, done)` posts a persisted GraphQL query (JSON) to
`https://api-partner.spotify.com/pathfinder/v2/query` with the same tokens. Before that it gets the tokens:
client-token (solving hashcash when asked, kept `refresh_after_seconds` less a minute), then login5
with the session's reusable credentials (kept `expires_in` less a minute). Requests that come while
the tokens are fetched wait for them. A 401 forgets the bearer and sends once more; a 429 with a
`Retry-After` of at most 5 s is sent once more after it. Everything else reaches `done` as it is:
`WebReply` with the status (0 without one: `network`), the body and the error text.

## `FileDownload`

1. `prepare()`: extended-metadata `TRACK_V4`, the Ogg Vorbis file for the preferred bitrate (SPOT-13,
   none: a `Track` failure, SPOT-15), its audio key (a refused key: `KeyRefused`), storage-resolve.
   Then `prepared()`; `kbps()` is the file's.
2. `start()`: range requests from the first CDN link. `received` carries the decrypted bytes after
   Spotify's header, in order; `normalisation()` is read from that header. A link that fails, answers
   with another status than 200 or 206, or breaks off midway hands over to the next link at the same
   byte (SPOT-14); after the last one it is `failed` with `Network` (the network failed) or `Track`.
   `completed()` when the file is whole.

## Traps

- The browser login's redirect URI must be `http://127.0.0.1:<port>/login`: Spotify's desktop client ID
  allows nothing else, so a login from another machine has to use the code (SPOT-01).
- login5 needs the reusable credentials, not the OAuth token: `Web` is useless until the session has
  logged in once.
- A CDN that ignores `Range` and answers 200 is read from its start; the bytes before the offset are
  dropped.
- Tests: `tests/spotify_client_test.cpp` (against `MockHttpServer` and `Tests::FakeAccessPointServer`;
  with `QIYAA_SPOTIFY_CREDENTIALS=<saved credentials>` it also logs in to Spotify and downloads a
  track, `QIYAA_SPOTIFY_TRACK` and `QIYAA_SPOTIFY_OUTPUT` say which and where).
