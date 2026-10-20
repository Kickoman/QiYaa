# `src/sources/local` — the user's own files as a source

The user's own audio files as one of the [music sources](../README.md), described by
[spec/player/local.md](../../../spec/player/local.md). So far the module reads a file's tags and
cover through TagLib; the source itself (opening files and folders, the `Core::TrackSource`) comes
next in [#30](https://github.com/Kickoman/QiYaa/issues/30). It does not decode audio
([src/audio](../../audio/README.md)). Namespace `LocalSource`, library `qiyaa_sources_local`.

| File | Contains |
|---|---|
| `tags.h/.cpp` | `FileTags`, `ReadTags`, `kMaxPictureBytes`, `kMaxReadBytes`: what a file's tags say about it, through TagLib |

## Dependencies

- `qiyaa_sources_local` links PUBLIC `qiyaa_core` and `Qt6::Core`, and PRIVATE `TagLib::tag`.
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
- **Threads.** It may be called on any thread, one call at a time per file. TagLib 2.3.2 guards
  its shared caches.
- **Debug output.** TagLib's own debug output goes to `qDebug` through one listener, set on the
  first call.

The test is `tests/local_tags_test.cpp`. It writes tags with TagLib into copies of the test
fixtures and reads them back.
