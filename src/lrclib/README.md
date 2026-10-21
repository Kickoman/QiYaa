# `src/lrclib` — LRCLIB, the lyrics service

[LRCLIB](https://lrclib.net) as the lyrics view's third-party service
([spec/player/lyrics.md](../../spec/player/lyrics.md#lrclib)): an open database of lyrics with and
without times, asked only when the user turns it on and only for a track whose source has no synced
text (`Core::LyricsFinder` decides when). `Service` is a `Core::LyricsService`; `search.h` turns a
reply into the answer. It is not a music source, so it is not under `src/sources/`. Namespace
`Lrclib`, library `qiyaa_lrclib`.

| File | Contains |
|---|---|
| `search.h/.cpp` | `Record`, `ParseSearch`, `Choice`, `Pick`, `TitleWithoutVersion`, `kDurationToleranceMs`, `kMaxReplyBytes`: a search reply and the record it gives |
| `service.h/.cpp` | `Service`, the `Core::LyricsService` that searches LRCLIB, tries again and cancels; `kBaseUrl` |

## Dependencies

- `qiyaa_lrclib` links PUBLIC `qiyaa_core`, `Qt6::Core` and `Qt6::Network`; only `app` links it
  ([docs/architecture.md](../../docs/architecture.md#modules)).
- It includes only `core/` and its own folder, holds no text for the user and throws nothing.

```bash
grep -rn --include='*.cpp' --include='*.h' '#include "' src/lrclib/ | grep -v '"\(core\|lrclib\)/'   # must print nothing
grep -rnw --include='*.cpp' --include='*.h' 'throw\|catch' src/lrclib/                              # must print nothing
grep -rnE --include='*.h' --include='*.cpp' '\btr\(|translate\(' src/lrclib/                       # must print nothing
```

## `search.h`: a reply and its record

```cpp
inline constexpr qint64 kDurationToleranceMs = 2000;
inline constexpr qsizetype kMaxReplyBytes = 2 * 1024 * 1024;

struct Record { qint64 id; double durationSeconds; bool instrumental; QString plainLyrics; QString syncedLyrics; };
struct Choice { Core::LyricsAnswer answer; std::optional<qint64> recordId; };   // recordId: none for Missing

QList<Record> ParseSearch(const QByteArray& body);    // anything but a JSON array: none
Choice Pick(const QList<Record>& records, qint64 durationMs);   // durationMs <= 0: unknown
QString TitleWithoutVersion(const QString& title);    // "A (Live)" -> "A"; empty: nothing to cut
```

- **`Pick`** (LYR-04, LYR-05) takes the records within `kDurationToleranceMs` of the track, or all
  of them when its duration is unknown. Of those: the first whose `syncedLyrics` parses as synced;
  else the first with `plainLyrics`, or with a `syncedLyrics` without a single time tag; else
  `Instrumental` when one of them says so; else `Missing`. Every answer is
  `LyricsFrom::ThirdParty`. `tests/lrclib_test.cpp` checks it against every pick of
  `spec/expected/lrclib`.
- **`TitleWithoutVersion`** cuts one trailing ` (…)` or ` […]`, the version a catalogue adds to a
  title, and gives empty when there is none or nothing would be left.

## `service.h`: `Service`

```cpp
inline const QString kBaseUrl = QStringLiteral("https://lrclib.net");

class Service : public Core::LyricsService {
public:
    Service(QNetworkAccessManager* network, QString baseUrl, const QString& appVersion, QObject* parent = nullptr);
    void lyrics(const Core::Track& track, Core::TLyricsDone done) override;
    void cancel() override;
    void setRetryDelayMs(int ms);                      // 1 s unless a test sets it
};
```

- **The request** (LYR-04): `GET <baseUrl>/api/search?track_name=<title>&artist_name=<first
  artist>`, both percent-encoded whole (a `+` or `&` in a name stays itself), with the
  `User-Agent` `QiYaa/<appVersion> (https://github.com/Kickoman/QiYaa)` that LRCLIB asks of its
  clients, and an 8 s transfer timeout. Without a title or an artist nothing is sent and the answer
  is `Missing`, on the next pass of the event loop.
- **Tries.** A 5xx, or no whole answer (a network failure, the timeout, a reply cut off after its
  status line), is tried again once, after the retry delay. Then a 503 counts as nothing found,
  with `serverError` set, and anything else is `Failed`, which `LyricsFinder` does not keep. Nothing found, with a title that has a version, searches once more by
  `TitleWithoutVersion`, with its own retry. Any other status, or a reply with no record that
  `Pick` takes, is nothing found.
- **Size.** A reply over `kMaxReplyBytes` is aborted as it comes and is `Failed`; it is not tried
  again.
- **One request at a time.** `lyrics` cancels the one before. `cancel()` aborts the request and
  any retry, and the cancelled `done` never runs. The destructor cancels too.
- `done` runs once, from the event loop, never inside `lyrics`. The `QNetworkAccessManager` must
  outlive the service and be used on its thread.

**Traps:**
- **A miss often comes as 503.** On 2026-10-21 LRCLIB answered some searches that find nothing
  with 503 `ServerOverloaded` rather than `[]`, again and again for the same query. So a final 503
  is "nothing found" for the view and the memory of `LyricsFinder`, and a failure for the
  telemetry (`serverError`).
- **Why the search and not `/api/get`.** `/api/get` wants the album exactly as LRCLIB has it, and a
  Yandex album name often differs (404). The search matches by title and artist, and `Pick` checks
  the duration itself.
- **Only the first artist.** Several artists joined with commas find nothing.
- **What LRCLIB learns:** the title and the first artist of each track asked about, and the
  computer's address. `docs/features.md` tells the user so.
