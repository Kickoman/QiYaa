# `src/sources/local` — the user's own files as a source

The user's own audio files as one of the [music sources](../README.md), described by
[spec/player/local.md](../../../spec/player/local.md).
- `Catalog` turns files or a folder the user opens into the `Core::Player`'s queue: it reads them
  on its own thread, with each file's tags and cover.
- `Source` is their `Core::TrackSource`: it opens a file on disk for the engine to read in place,
  and reads a file's lyrics from an `.lrc` next to it and from its tags
  ([spec/player/lyrics.md](../../../spec/player/lyrics.md), LYR-10).

The module does not decode audio ([src/audio](../../audio/README.md)), shows no dialog (the menu
items are `App::MainMenu`'s, [src/app](../../app/README.md)) and knows nothing of Yandex Music. A
queue of local files has no wave from history (CONT-12). Namespace `LocalSource`, library
`qiyaa_sources_local`.

| File | Contains |
|---|---|
| `catalog.h/.cpp` | `Catalog`: opening files or a folder with a ticket each, on a thread of its own, and the statuses |
| `scan.h/.cpp` | `ScanFiles`, `ScanFolder`, `ScanResult`, `kMaxFiles`, `kMaxFolders`, `kCoverPixels`: the files to queue, in order, with their tags and covers |
| `lyrics.h/.cpp` | `LrcPathOf`, `ReadLrc`, `ReadLyrics`: a file's lyrics from its `.lrc` and its tags, in the order of LYR-10 |
| `source.h/.cpp` | `Source`, the `Core::TrackSource` of local files: the cover, opening the file, the lyrics on a thread of their own |
| `tags.h/.cpp` | `FileTags`, `ReadTags`, `ReadTagLyrics`, `kMaxPictureBytes`, `kMaxReadBytes`: what a file's tags say about it, through TagLib |
| `tracks.h/.cpp` | `kSourceName`, `kExtensions`, `HasAudioExtension`, `TrackOf`: a file as a `Core::Track` |

## Dependencies

- `qiyaa_sources_local` links PUBLIC `qiyaa_core` and `Qt6::Core`, and PRIVATE `TagLib::tag`. `app`
  links it ([docs/architecture.md](../../../docs/architecture.md#modules)); nothing else does.
- TagLib 2.3.2 is fetched by the top-level `CMakeLists.txt` and linked statically, the same build on
  every platform: without zlib, and with only the formats the engine plays (MPEG, FLAC and Ogg,
  RIFF, MP4). Without APE, its MPEG reader also skips APE tags: an mp3 whose tags are only APEv2
  reads like one without tags. Only `tags.cpp` and the tests include its headers, by their bare names
  (`<fileref.h>`), so the module's header shows nothing of TagLib.
- It includes only `core/`, `audio/` and its own folder.

```bash
grep -rn --include='*.cpp' --include='*.h' '#include "' src/sources/local/ | grep -v '"\(core\|audio\|sources/local\)/'   # must print nothing
grep -rl --include='*.cpp' --include='*.h' '#include <fileref.h>' src/ | grep -vx 'src/sources/local/tags\.cpp'          # must print nothing
grep -rnw --include='*.cpp' --include='*.h' 'throw\|catch' src/sources/local/                                    # must print nothing
```

## `tags.h`: `ReadTags`

```cpp
inline constexpr int64_t kMaxPictureBytes = 16 * 1024 * 1024;
inline constexpr int64_t kMaxReadBytes = 64 * 1024 * 1024;

struct FileTags {
    QString title;            // empty: none
    QStringList artists;      // each ARTIST value, in order
    QString album;
    int year = 0;             // 0: none
    QString genre;
    int64_t durationMs = 0;   // from the audio itself; 0: unknown
    bool playable = true;     // false: a format or codec the engine does not play (ALAC in MP4)
    QByteArray picture;       // a JPEG or PNG; empty: none
};

std::optional<FileTags> ReadTags(const QString& path);   // nullopt: not a file TagLib knows
QString ReadTagLyrics(const QString& path);              // empty: none
```

- **What is read** (LOCAL-04).
  - The title, album and genre are trimmed, and the year is the tag's (a full date gives its
    year).
  - Each `ARTIST` value is one artist, so a tag that keeps several apart gives several.
  - The duration is the audio's own, not a tag's.
  - Nothing is made up: a file without a title gives an empty one, and the source names it.
- **The picture** (LOCAL-05).
  - It is the front cover when the format says which one that is, and otherwise the first
    picture. MP4 keeps no picture types.
  - Only JPEG and PNG count, by their first bytes rather than the stated MIME type.
  - A picture over `kMaxPictureBytes` is left out, and the other tags are read as usual.
- **`playable`** (LOCAL-02). It is true for MPEG audio, FLAC, WAV, and MP4 with AAC, which are the
  files the engine decodes. It is false for an MP4 with another codec (ALAC, for one) and for ADTS
  AAC, which TagLib opens as an MPEG file but the engine would hand to its mp3 decoder.
- **`nullopt`**: a file that cannot be opened (missing, a folder, no permission), an empty file, or
  one of no format TagLib knows.
- **Sizes are bounded before allocation.**
  - TagLib reads through `BoundedStream`, a `TagLib::FileStream` that gives nothing for a block over
    `kMaxReadBytes`. TagLib allocates for what it reads, so no structure of the file can make it
    allocate more.
  - The trade-off: a tag read in one block, such as an ID3v2 tag with its pictures, is lost whole
    when it is larger than that. Such a file reads like one without tags.
- **`ReadTagLyrics`** (LYR-10) gives the first of TagLib's `LYRICS` and `LYRICS:<description>`
  keys, as it is: USLT in an ID3v2 tag of an mp3 or WAV file (TagLib names a frame with a
  description other than empty or `LYRICS` by it), the `LYRICS` Vorbis comment of FLAC, `©lyr` of
  MP4. It reads through the same `BoundedStream`, and opens the file a second time: the lyrics are
  read only when the lyrics view asks, never during a scan.
- **Threads.** Both may be called on any thread, one call at a time per file. TagLib 2.3.2 guards
  its shared caches.
- **Debug output.** TagLib's own debug output (only in a debug build of it) goes to the logging
  category `qiyaa.taglib`, off by default (`QT_LOGGING_RULES=qiyaa.taglib.debug=true` shows it),
  through one listener set on the first call.

The test is `tests/local_tags_test.cpp`. It writes tags with TagLib into copies of the test
fixtures and reads them back.

## `tracks.h`: a file as a track

```cpp
inline const QString kSourceName = QStringLiteral("local");
inline const QStringList kExtensions = {"mp3", "flac", "wav", "m4a", "mp4"};
bool HasAudioExtension(const QString& path);           // by its suffix, ignoring case
Core::Track TrackOf(const QString& path, const std::optional<FileTags>& tags, const QString& cover);
```

- The key is `{"local", <absolute path>}`: `QFileInfo::absoluteFilePath`, cleaned and with `/`, not
  resolved through links. In a jam the host reports such a track with an id of its own, never the
  path (HOST-37, [src/jam](../../jam/README.md)).
- The title is the tag's, or the file's name without its last extension (LOCAL-04). The artists,
  album, year, genre and duration come from the tags, and stay empty without them.
- `Track::cover` holds the path of the cover file `ScanFiles` and `ScanFolder` wrote, or nothing.

## `scan.h`: what gets queued

```cpp
inline constexpr int kMaxFiles = 10'000;
inline constexpr int kMaxFolders = 10'000;
inline constexpr int kCoverPixels = 400;
struct ScanResult { QList<Core::Track> tracks; bool cut = false; };   // cut: some were left out
ScanResult ScanFiles(const QStringList& paths, const QString& coverDirectory, const std::atomic<bool>& cancelled);
ScanResult ScanFolder(const QString& folder, const QString& coverDirectory, const std::atomic<bool>& cancelled);
```

- **What is taken** (LOCAL-01, LOCAL-02).
  - Files with one of `kExtensions` (`ScanFiles` drops anything else it is given).
  - `ScanFolder` goes through every subfolder, but skips hidden files and folders (hidden by the
    system, or with a name that starts with a dot, on every system) and links to folders
    (symbolic links, and junctions on Windows), which also rules out loops. `ScanFiles` takes a
    hidden file the user picked.
  - A file whose tags say the engine cannot play it (`FileTags::playable`, ALAC in MP4) is left
    out. A file TagLib cannot read is kept, titled by its name: if it does not decode either, that
    is a track failure when it plays (LOCAL-07).
- **Order.** The paths' natural order, component by component, and a folder's name competes with
  its siblings' names. `ScanFolder` gets there by walking depth first with each folder's entries,
  files and folders together, sorted by name in that order, so the walk itself meets the files in
  order. `ScanFiles` sorts the picked paths the same way, component by component.
  - Each name is split into runs of ASCII digits and of everything else. Digits come before text.
    Digit runs compare by value, so "2" comes before "10" and "02" equals "2". Text runs compare
    case-folded with a `QCollator` of the system's locale.
  - Names equal so far compare as plain strings, so the order is total.
  - `QCollator`'s own numeric mode is not used: it does nothing in the C locale (a CI runner, a
    service), where "02" would then sort before "1".
- **Limits** (LOCAL-02). At most `kMaxFiles` files are taken, the first in that order (both
  functions cut after ordering), and `ScanFolder` visits at most `kMaxFolders` folders. What lies
  beyond is left out, and `cut` says so.
- **Cancelling.** `cancelled` is read between folders and between files. Once it is set the result
  is empty.
- **Covers** (LOCAL-05).
  - The picture in a file's tags, or else its folder's `cover`, `folder` or `front` image (`.jpg`,
    `.jpeg`, `.png`, any case; the first name in that order wins). A folder's image is looked up
    once per scan.
  - Either is read whole, but at most `kMaxPictureBytes`, then decoded with `QImageReader`. An
    image over `kCoverPixels` on its longer side is scaled down to it.
  - It is written as a JPEG to `<coverDirectory>/<sha1 of the picture's bytes>.jpg`
    (`QSaveFile`), unless that file is already there. The same picture in many files, and the
    same name of different albums, therefore never collide.
  - An empty `coverDirectory`, a picture that does not decode or a failed write give no cover.
    Nothing ever deletes these files.
- They run on any thread, with nothing shared but the files they write. TagLib and `QImage` are
  reentrant.

## `source.h`: `Source`

```cpp
class Source : public Core::TrackSource {
    QUrl coverUrl(const Core::Track& track, int pixels) const override;   // file: URL of Track::cover
    QUrl webPage(const Core::Track& track) const override;                // always empty (LOCAL-08)
    void open(const Core::Track& track, TOpened done) override;
    void trackEvent(Core::TrackEvent, const Core::Track&, double) override;   // nothing to report (LOCAL-08)
    void lyrics(const Core::Track& track, Core::TLyricsDone done) override;
};
```

- `coverUrl` is the `file:` URL of the track's cover file, or empty. `CoverCache` reads such a URL
  in place ([src/core](../../core/README.md)). `MediaControls` gives it to MPRIS as `artUrl` but
  keeps it from SMTC ([src/integrations](../../integrations/README.md)).
- `open` answers on the next pass of the event loop (the port's contract). It answers with
  `OpenedTrack::file`, an `Audio::FileByteSource` of the path (LOCAL-06), its `codec` the
  suffix, and `bitrateKbps` from the size and the duration (0 without one). The `link` is empty: a
  jam's guests cannot fetch a local file (LISTEN-14).
- That bitrate is an estimate: the tags and pictures in the file count too, so an mp3 with a large
  cover shows more than its audio's. TagLib knows the audio's own, but `Core::Track` has no field
  to carry it from the scan to `open`. WAV and FLAC are often over 999 kbps, which the main window
  shows in hundreds ("14H", `Ui::BitrateText`).
- A file that cannot be opened is `TrackFailure{Track, "cannot open the file <name>"}` (LOCAL-07).
  `Network` is never answered, so no network wait applies.
- A source destroyed before it answers drops the answer.
- **`lyrics`** (LYR-10) reads on a new `QThread` per call and answers on the source's thread
  through a queued call, so never inside the call. `ReadLyrics` does the reading:
  - **The `.lrc`.** `LrcPathOf` finds it: the file in the same folder whose name without the
    extension is the track's and whose extension is `.lrc` in any case. `ReadLrc` decodes it: by
    its byte order mark, else as UTF-8, else in the system's code page. A file over
    `Core::kMaxLyricsBytes` is not read (LYR-11).
  - **The order.** A synced `.lrc` (`LrcFile`), then the tags' lyrics (`FileTags`, synced or
    plain), then a plain `.lrc`; otherwise `Missing`. A synced `.lrc` spares the tags' read.
  - A file that cannot be read gives `Missing`, never `Failed`: asking again would not help.
  - Another source's track gets `Missing` on the next pass of the event loop.
  - The destructor waits for every reading thread, so an answer never reaches a destroyed source,
    and one that was still queued is dropped with it.

## `catalog.h`: `Catalog`

```cpp
class Catalog : public QObject {
    Catalog(Core::Player* player, QString coverDirectory, QObject* parent = nullptr);
    void openFiles(const QStringList& paths);   // nothing for an empty list
    void openFolder(const QString& path);       // nothing for an empty path
Q_SIGNALS:
    void noticed(const Core::SourceNotice& notice);
};
```

- **A pick is a source request** (SRC-01, LOCAL-03). Each call takes `Player::newSourceRequest()`
  at once and says `Loading` with its name, then reads on a new `QThread`.
  - A newer call cancels the one before.
  - Any newer source request (another source, `Player::clear`, logging in to Yandex Music)
    makes the result stale, and a stale result is dropped without a word.
- **The name** (LOCAL-01). A single file's name without its extension, several files' first
  folder, or the folder's own name.
- **The result.**
  - Nothing to queue: `Empty` with the name, and the queue stays (LOCAL-03, SRC-07).
  - Otherwise the tracks replace the queue as a head, `waveFromHistory = false` (CONT-12), and play
    from the first. Then `Loaded`, or `LoadedFirst` when `cut`, with the count.
- **Threads.** The scan runs on its thread; the result comes back to the catalog's thread through a
  queued call. The destructor cancels the scan and waits for its thread, which stops at the next
  file or folder.
- `coverDirectory` comes from `src/app` (`LocalCoversDirectory`): nothing in this module knows
  where QiYaa keeps its files.

**Traps:**
- A whole folder of files with tags is read one file after another before anything plays. On a
  slow disk this takes seconds, during which only the `Loading` status shows.
- The key is the path: a file that moves or is renamed becomes another track. A file changed in
  place plays as it is then.

The test is `tests/local_source_test.cpp`.
