# `src/audio` — playback engine, equalizer and Winamp EQ presets

Plays one audio file while it is still downloading: the bytes the caller appends go into an
in-memory `StreamBuffer`, a decoder thread turns them into float stereo PCM at the output device's
rate, a 2-second queue of blocks carries that PCM to the miniaudio device callback together with
what it is (track, position, start or seek), and the callback applies the equalizer, feeds the
visualization tap and applies volume and balance. A second stream can be queued; it follows the
current one in the same queue without a gap. It reads mp3, FLAC,
WAV and AAC in MP4 (the economy quality of [spec/player/quality.md](../../spec/player/quality.md)). The module also holds the
10-band equalizer DSP, Winamp's built-in presets and the `.eqf` reader and writer. It does not
download anything, decide what plays next or when to queue it, or poll itself: that is `Player` in
[src/core](../core/README.md). It draws no visualization ([src/vis](../vis/README.md)) and shows no
equalizer window ([src/ui](../ui/README.md)). Namespace `Audio`, library `qiyaa_audio`.

| File | Contains |
|---|---|
| `audio_engine.h/.cpp` | `AudioEngine` — streams, state, seeking, volume, balance, `poll()`. Only in the `.cpp`: `AudioEngine::Implementation` (miniaudio context and device, the device callback) |
| `blocks.h` | `Block`, `BlockQueue` (decoder → callback), `PlaybackSnapshot`, `PlaybackState` (callback → UI), `Older` |
| `decoder.h/.cpp` | `Decoder` — the decoder thread: its commands and events, every `ma_decoder`, gapless chaining. Only in the `.cpp`: `Decoder::Source` |
| `stream_buffer.h/.cpp` | `ByteSource`, `StreamBuffer` — what the decoder reads, one downloading file |
| `equalizer.h/.cpp` | `EqSettings`, `EqualizerDsp` — preamp and 10 peaking biquads per channel, coefficients handed to the audio thread through a triple buffer; `kEqBands`, `kEqBandHz`, `kEqMaxDb` |
| `eq_presets.h/.cpp` | `EqPreset`; `ParseEqf`, `WriteEqf` (`.eqf` and `.q1`); `EqfToDb`, `DbToEqf`; `BuiltinEqPresets` — Winamp's 17 presets |
| `mp4_aac_backend.h/.cpp` | `IsMp4`, `Mp4AacBackend` — a miniaudio decoding backend for AAC in MP4 over minimp4 and fdk-aac. Only in the `.cpp`: `Mp4AacDecoder` and the `DataSource` miniaudio holds |
| `vis_tap.h/.cpp` | `VisTap` — the last 4096 output frames, for visualizations |
| `error.h` | `Audio::Error` |
| `miniaudio_impl.c` | `#define MINIAUDIO_IMPLEMENTATION` + `#include "miniaudio.h"`: the one translation unit that compiles miniaudio. It is the source of `qiyaa_miniaudio`, not of `qiyaa_audio` |
| `minimp4_impl.c` | `#define MINIMP4_IMPLEMENTATION` + `#include "minimp4.h"`: the source of `qiyaa_minimp4` |

## Dependencies

- `qiyaa_audio` links PUBLIC `Qt6::Core` (`QObject`, `QString`, `QStringList`, `QByteArray`,
  `QHash`, `QList`, `QStringConverter`, `QStringDecoder`) and PRIVATE `qiyaa_miniaudio`,
  `qiyaa_minimp4` and `qiyaa_fdk_aac`.
- `qiyaa_minimp4` is built from `src/audio/minimp4_impl.c` against `contrib/minimp4/minimp4.h`
  (lieff/minimp4 `2a3014a`, CC0). `qiyaa_fdk_aac` is fdk-aac 2.0.3's decoder libraries only,
  built by `contrib/fdk-aac/CMakeLists.txt` (QiYaa's own; upstream's source lists) with
  `-fno-exceptions -fno-rtti`, as upstream does. Neither gets the project's warning flags.
- `qiyaa_miniaudio` is a separate static library built from `src/audio/miniaudio_impl.c` against
  `contrib/miniaudio/miniaudio.h` (miniaudio 0.11.25) with `MA_NO_ENCODING MA_NO_GENERATION
  MA_NO_VORBIS`, so the decoders compiled in are WAV, FLAC and MP3. It links `Threads::Threads` and
  `${CMAKE_DL_LIBS}`; on Apple it defines `MA_NO_RUNTIME_LINKING` and links CoreFoundation,
  CoreAudio and AudioToolbox, on other Unix systems `m`. It does not get the project's warning
  flags (`qiyaa_target_defaults` is not applied to it).
- It deliberately links no other project module and neither `Qt6::Gui`, `Qt6::Widgets` nor
  `Qt6::Network`. It sits at the bottom of the module order; which modules link it is in
  [docs/architecture.md](../../docs/architecture.md#modules).
- miniaudio stays private to the module: `audio_engine.h` and every header another module may
  include leave out `miniaudio.h`. `AudioEngine` holds everything miniaudio-typed through
  `std::unique_ptr<Implementation>` and a forward-declared `StreamBuffer`; `decoder.h` keeps its
  `ma_decoder`s in a `Source` defined in `decoder.cpp`. Only the module's own `mp4_aac_backend.h`
  includes it (miniaudio's vtable types are anonymous structs that cannot be forward-declared);
  `decoder.cpp` and `tests/mp4_aac_test.cpp` include that header. minimp4 and fdk-aac appear only
  in `mp4_aac_backend.cpp`. miniaudio itself: `audio_engine.cpp` (the device), `decoder.cpp` (the
  decoders), `mp4_aac_backend.*` and `miniaudio_impl.c`.
- Inside the module: `audio_engine.h` and `eq_presets.h` include `equalizer.h`; `decoder.h`
  includes `blocks.h` and `stream_buffer.h`; `audio_engine.cpp` includes `blocks.h`, `decoder.h`,
  `equalizer.h`, `stream_buffer.h` and `vis_tap.h`; `decoder.cpp` includes `mp4_aac_backend.h`;
  `eq_presets.cpp` includes `error.h`. `blocks.h`, `stream_buffer.h`, `equalizer.h`, `vis_tap.h`
  and `error.h` include no file of the module.

```bash
grep -rnE '#include "(app|core|integrations|skins|ui|vis|yandex)/' src/audio/ --include='*.h' --include='*.cpp'   # must print nothing
grep -rlE '#include [<"]miniaudio' src/ --include='*.h' --include='*.cpp' --include='*.c' \
    | grep -vxE 'src/audio/(audio_engine\.cpp|decoder\.cpp|miniaudio_impl\.c|mp4_aac_backend\.(h|cpp))'  # must print nothing
grep -rlE '#include [<"](minimp4|aacdecoder_lib)' src/ --include='*.h' --include='*.cpp' --include='*.c' \
    | grep -vxE 'src/audio/(mp4_aac_backend\.cpp|minimp4_impl\.c)'                                  # must print nothing
grep -rl '"audio/mp4_aac_backend.h"' src/ --include='*.h' --include='*.cpp' \
    | grep -vxE 'src/audio/(decoder|mp4_aac_backend)\.cpp'                                           # must print nothing
grep -rnE '#include <Q' src/audio/ --include='*.h' --include='*.cpp' \
    | grep -vE '<Q(ByteArray|Hash|List|Object|String|StringConverter|StringDecoder|StringList)>'                              # must print nothing
grep -rnw 'throw' src/audio/ --include='*.h' --include='*.cpp' | grep -v '^src/audio/eq_presets.cpp:'   # must print nothing
```

The tests are `tests/audio_test.cpp` (the real engine on `tests/data/sine440_3s.mp3` and
`sine440_3s_heaac.m4a`), `tests/mp4_aac_test.cpp` (the MP4 backend through an `ma_decoder` over
memory) and `tests/dsp_test.cpp` (equalizer response, presets, `.eqf`). CTest runs every test with
`QIYAA_AUDIO_BACKEND=null`.

## `audio_engine.h` — `AudioEngine`

```cpp
class AudioEngine : public QObject {
public:
    enum class State { Stopped, Buffering, Playing, Paused };

    struct InitResult {
        bool ok = false;
        QString message;
    };
    InitResult init();                 // opens the output device; after a success: {true, {}}
    QString backendName() const;       // miniaudio's name ("PulseAudio", "Null"...), "none" before init

    using TStreamId = quint64;         // 0 = none

    TStreamId beginStream(double startSeconds = 0);   // drops current and queued; 0 without a device
    TStreamId queueStream();           // 0 without a current stream
    void clearQueued();
    TStreamId queuedStream() const;    // 0 when none, or the decoder could not open it
    TStreamId playQueuedNow();         // 0 when queuedStream() is 0

    void appendData(TStreamId stream, const QByteArray& bytes);   // unknown id: no-op
    void finishData(TStreamId stream);
    void failData(TStreamId stream);
    void appendData(const QByteArray& bytes);                     // the current stream
    void finishData();
    void failData();
    TStreamId currentStream() const;

    void pause();
    void resume();
    void stop();
    bool seek(double seconds);         // false without a current stream

    State state() const;
    double positionSeconds() const;
    int sourceSampleRate() const;      // the current file's own format; 0 until the decoder opened it
    int sourceChannels() const;

    void setVolume(int percent);       // clamped to 0..100, default 75
    void setBalance(int balance);      // clamped to -100 (left) .. 100 (right)
    void setEqualizer(const EqSettings& settings);

    void readVisSamples(std::span<float> left, std::span<float> right) const;   // <= 4096 frames
    VisReadResult readNewVisSamples(uint32_t cursor, std::span<float> stereo) const;
    uint32_t visCursor() const;
    int outputSampleRate() const;      // the device's rate; 44100 before init()

    void poll();                       // call from a UI timer

Q_SIGNALS:
    void stateChanged(Audio::AudioEngine::State state);
    void trackFinished();
    void trackAdvanced();
    void errorOccurred(const QString& message);
    void streamUndecodable(Audio::AudioEngine::TStreamId stream);
};
```

### Pipeline

```
appendData() ─> StreamBuffer ─(decoder thread: ma_decoder)─> blocks ─> device callback ─> device
                                                             2 s       EQ → VisTap → gains, clip
```

Every frame count in the engine is in device-rate frames: each `ma_decoder` converts its source to
32-bit float, 2 channels, at the device's sample rate. `sourceSampleRate()` and `sourceChannels()`
report the file's own format, for display.

### Streams

- `beginStream()` drops the current and the queued stream, creates the next id (ids start at 1
  and are never reused by one engine), starts the device if it is stopped, and sets `Buffering`.
  Without a device (`init()` not called or failed) it sets `Stopped`, emits
  `errorOccurred("no audio output device")` and returns 0. With `startSeconds > 0` decoding starts
  there and `positionSeconds()` shows that position from the start: how the `Player` resumes a
  track whose download broke off.
- `appendData()`, `finishData()`, `failData()` do nothing for an id that is neither current nor
  queued, so a late network callback cannot feed the wrong track. `failData()` ends a stream the
  way `finishData()` does: the engine plays up to the last byte received and reports
  `trackFinished` as usual. Telling the user is the caller's job.
- `queueStream()` replaces the queued stream (it calls `clearQueued()` first) and returns 0 without
  a current stream, i.e. before `beginStream()`/`playQueuedNow()` and after `stop()`.
- `playQueuedNow()` makes the queued stream current and plays it from byte 0 (`Buffering`, no
  `trackAdvanced`).
- `stop()` drops every stream, stops the device, resets the position to 0 and sets `Stopped`.
- Streams are held whole in memory: the current and the queued file. After a gapless advance the
  decoder releases the previous file when it hears of the advance.

### State and signals

| State | Entered by |
|---|---|
| `Stopped` | construction, `stop()`, `poll()` after the current stream failed to decode, `beginStream()` without a device |
| `Buffering` | `beginStream()`, `playQueuedNow()`; `resume()` while no audio of the current start or seek is ready |
| `Playing` | `poll()` once audio of the current stream has played; `resume()` when it has played or is ready to |
| `Paused` | `pause()` from `Playing` or `Buffering`; it stops the device, `resume()` starts it again |

All signals are emitted synchronously on the UI thread:

| Signal | From | When |
|---|---|---|
| `stateChanged` | `beginStream`, `playQueuedNow`, `pause`, `resume`, `stop`, `poll` | the state actually changes |
| `trackFinished` | `poll` | `Playing`, and the callback has reached the end of the current stream: its end marker, or a stream it may not go on to. Also when the callback had gone on into a stream `clearQueued()` dropped meanwhile. Once per start or seek |
| `trackAdvanced` | `poll` | the callback plays the queued stream: `currentStream()` is now the former queued id, `queuedStream()` is 0, the position restarts near 0. No `stateChanged` |
| `errorOccurred` | `beginStream`, `poll` | no device; the current stream cannot be decoded (after `stop()`, so `stateChanged(Stopped)` comes first) |
| `streamUndecodable(stream)` | `poll` | right after that `errorOccurred`, with the id of the stream that could not be decoded |

`poll()` emits at most one signal and returns right after it (`errorOccurred` comes with its
`stateChanged(Stopped)` and `streamUndecodable`), so a connected slot may call back into the
engine: `Player` starts the next track from its `trackFinished` slot. Nothing is reported between
polls; how often `Player` polls is in [src/core](../core/README.md).

### Position, volume, balance

`positionSeconds()` is the frame of the current stream the callback has played up to, divided by
the device rate. After `beginStream(startSeconds)`, `seek()`, `playQueuedNow()` and `stop()` it
shows the target (or 0) at once and keeps it until audio from there plays (TR-08). It stands still
during a pause and an underrun. Once the callback has gone on into the queued stream, and until
`poll()` moves over, it keeps the last value it saw of the old track.

Gain = (volume / 100)², so the default 75 gives 0.5625. With b = balance / 100, the left channel is
multiplied by 1 − b when b > 0 and the right by 1 + b when b < 0. After the gains every sample is
clamped to [−1, 1]. The equalizer and the tap come before the gains, so the tap does not follow the
volume.

### Threads

Each piece of state has one owner thread; the threads talk only through the channels below, and
what a block of audio is travels with it.

| Thread | Owns |
|---|---|
| UI thread: the thread that owns the engine (the Qt main thread in the app) | every public method and signal; the streams, their ids, the state, the generation and its target, what was reported; starting and stopping the device |
| Decoder thread: one per engine, from `init()` to the destructor | every `ma_decoder`: the current source, the one it was chained from until the UI moves on, the one being chained; the decision whether a track ends or the queued one follows |
| Device thread: miniaudio's, running the callback | which stream plays and where inside the front block; the equalizer's filter state; `VisTap::write()`. It takes no lock and allocates nothing |

| From → to | Channel | Carries |
|---|---|---|
| UI → decoder | commands, under the decoder's mutex and condition variable | `Start{stream, source, generation, frame}`, `Queue{stream, source}`, `ClearQueued{stream}`, `Seek{stream, generation, frame}`, `Advanced{stream}`, `Stop` |
| decoder → UI | events, under the same mutex, taken by `poll()` | `Opened{stream, rate, channels}`, `OpenFailed{stream}` |
| decoder → callback | `BlockQueue`: single producer, single consumer, 2 s at the device rate, allocated in `init()` | blocks |
| callback → UI | `PlaybackState`: one writer, a sequence lock | `{generation, stream, frame, ended}` |
| UI → callback | atomics | `generation`, `allowedNext` (the stream the callback may go on to), the gains; the equalizer through its triple buffer |
| UI → decoder | `StreamBuffer`, its own mutex | the downloaded bytes; `append` and `finish` wake the decoder |

The block, in `blocks.h`:

```cpp
struct Block {
    uint32_t generation;  // of the start or seek it answers
    uint64_t stream;
    int64_t frame;        // of the first sample in the track, in device frames
    uint32_t frameCount;  // 0..1024; 0 is a marker only
    bool endOfStream;     // nothing of this stream follows in this generation
    std::array<float, 1024 * 2> samples;
};
```

A track boundary never falls inside a block: the last block of a track may be short.

**Generations.** `beginStream()`, `playQueuedNow()`, `seek()` and `stop()` each start one, and so
does `poll()` when the callback went on into a cleared stream. The commands that start audio carry
it, and the decoder stamps it on every block. Older blocks are never heard.

**What the callback does**, once per device period:

1. It loads `generation` and drops every block older than that, which is only moving an index.
2. The first block of a newer generation plays from its `frame`. (A block can be newer than the
   `generation` the callback loaded: the decoder may have written it in between.)
3. A block of another stream in the same generation is played without a gap if that stream is
   `allowedNext`, loaded at that moment; otherwise the track has ended and silence follows.
4. After an `endOfStream` block the track has ended and silence follows.
5. No block: an underrun, silence, and the position stands.
6. It publishes `{generation, stream, frame, ended}` when that changed, then applies the
   equalizer, copies into `VisTap` and applies the gains, as before.

**Invariants.**
- Every field outside these channels is written by one thread. Every atomic the callback touches is
  `static_assert`ed to be always lock-free.
- The queue has one reader at a time: the callback while the device runs, the UI thread while it
  is stopped. The UI drops stale blocks in `pause()`, in `seek()` on pause and before it starts the
  device; `ma_device_stop()` and `ma_device_start()` order the handoff. The callback keeps its offset
  into the front block together with that block's index, so blocks the UI dropped reset it.
- The device is started by `beginStream()`, `playQueuedNow()` and `resume()`, and stopped by
  `pause()`, `stop()` and the destructor. `init()` does not start it, and nothing stops it to
  synchronise.
- `poll()` reads the callback's state before the decoder's events: an `Opened` or `OpenFailed`
  sent before the blocks the callback has played is then already among them.
- Lock order: the decoder's mutex, then a `StreamBuffer`'s. `StreamBuffer` wakes the decoder after
  it has released its own.

### Decoder thread

It opens a source on its own: it reads the first 8 bytes (waiting for them). An MP4 file
(`IsMp4`: `ftyp` at offset 4) gets `Mp4AacBackend()` as miniaudio's only custom backend, and the
decoder is kept only if that backend opened it: miniaudio would otherwise go on to its MP3
decoder, which finds "frames" in an MP4 it does not understand. Anything else first forces the MP3
decoder, then miniaudio's format detection (WAV, FLAC, MP3). When none opens it the current
stream gets `OpenFailed`, and `poll()` stops the engine and reports the error.

Reads and byte seeks wait for bytes on the decoder's condition variable. A command that seeks or
drops the source being read (`Start`, `Seek`, `Stop`, shutting down) makes them return
`MA_CANCELLED` at once; such a source is sought again or dropped before it reads on, and an open
cut short that way is tried again, not reported. So a seek into the downloaded part plays at once
while the download is stalled (TR-09). Commands are handled between blocks. While the queue is full
or it waits for a successor, the decoder looks again every 10 ms (the callback wakes no one). It
decodes ahead of playback by at most the queue, 2 s.

### AAC in MP4 (`mp4_aac_backend`)

`Mp4AacBackend()` is a `ma_decoding_backend_vtable` with only `onInit` and `onUninit`: it reads
through the read and seek callbacks miniaudio passes, which in the engine are the decoder's, over
a `ByteSource`.
`onInit` builds a `DataSource` (miniaudio's `ma_data_source_base` first, then a pointer to the
C++ `Mp4AacDecoder`) and opens it:

1. **Where the index ends.** It walks the top-level boxes from byte 0 (8-byte headers, a 64-bit
   size when the size is 1, at most 64 boxes) until the end of `moov`. A box of size 0 (to the
   end of the file) or a size that overflows ends the walk without a file. Yandex's files are
   `ftyp, uuid, moov, mdat`, so this reads about 23 KB.
2. **minimp4** parses up to that end and no further (`MP4D_open(…, file_size = end of moov)`): it
   needs only the index, and reading on would wait for the whole `mdat` to download. A `moov`
   after `mdat` still works, after the download reaches it.
3. **The track**: the first one with handler `soun`, object type `0x40` (MPEG-4 audio), a
   decoder-specific info and samples. Anything else (FLAC in MP4, video) is refused, and
   miniaudio's other decoders do not get the file.
4. **fdk-aac**: `aacDecoder_Open(TT_MP4_RAW)`, `aacDecoder_ConfigRaw` with the track's
   AudioSpecificConfig, at least and at most 2 output channels (mono is doubled, so the format
   never changes mid-stream) and no limiter (its look-ahead adds delay). The first access unit is
   decoded at once: its stream info gives the rate (44,100 for Yandex's HE-AAC, after SBR), the
   frame size (2048) and `outputDelay`.

Output is 16-bit stereo at that rate; `ma_decoder` converts it like any other source. Access unit
`n` covers frames `[n × frameSize − outputDelay, (n + 1) × frameSize − outputDelay)` and frames
before 0 are dropped. The length is the smaller of `sampleCount × frameSize − outputDelay` and the
track's duration (`mdhd`) at that rate.

**Seeking** to frame `F` restarts at access unit `(F + outputDelay) / frameSize − 2`, clears the
transport buffer and decodes the first unit with `AACDEC_CLRHIST`; the frames before `F` are
dropped. Two units of run-up rebuild the overlap and the SBR state: with one, the first ~1,300
frames after the target differed from playing through; with two they match to 2·10⁻⁴
(`seekingLandsOnTheSameSamplesAsPlayingThrough`).

**Ends and failures.** A read that fails (a download cut short, a read the decoder gives up for a
command) ends the source where its data ends, like an mp3. A unit fdk-aac cannot decode is concealed by
fdk-aac; a change of rate or frame size mid-stream ends the source. Access units are bounded at
64 KiB before they are read.

Not supported: encrypted files (`encraw`), FLAC in MP4, more than 2 output channels, and the edit
list (`elst`): Yandex's files have `media_time` 0, so the encoder's priming is not trimmed.

### Gapless chaining

Only the decoder decides, when the current source ends:
- A queued stream that is complete (its download ended, successfully or not) and no source kept
  from an earlier chain: the decoder opens it, sends `Opened` and writes its blocks right after the
  old ones. Opening never waits for the network, since the file is all there. The old source is
  kept until the UI's `Advanced`.
- Otherwise it waits while more than max(100 ms, 2 device periods) of audio is queued, and then
  writes the end marker. A queued stream completing meanwhile still chains
  (`queuedStreamArrivingLateStillChains`); one that does not is started by the caller the usual
  way.
- A queued stream that does not open: `OpenFailed`, then the end marker. After the next `poll()`
  `queuedStream()` is 0 (`undecodableQueuedStreamIsSkipped`).

**Advance** (`poll()`). The callback's state shows the queued stream in the current generation:
it becomes current, the decoder gets `Advanced`, and `trackAdvanced` is emitted. The audio has no
gap because the blocks follow each other; the signal comes up to one poll interval later.

**Undo.** A seek within the track the UI shows, while the decoder has already chained the next,
makes the old source current again; the chained stream waits to be chained again from its start
(`seekingBackUndoesTheChain`).

**Cancel** (`clearQueued()`). `allowedNext` becomes 0 and the decoder gets `ClearQueued`. A stream
not chained yet is forgotten. A chained one is dropped, and an end marker with its id ends the
track where it would have begun (`clearingAChainedStreamStopsAtTheBoundary`); nothing chains again
in that generation. If the callback had already gone on into it, `poll()` starts a new generation
and reports `trackFinished`, and the rest of that stream is not heard
(`clearingAStreamTheCallbackAlreadyPlays`).

### Seeking

`seek(seconds)` returns false without a current stream or for `seconds < 0`. Otherwise it starts a
generation whose target is `int64(seconds × device rate)`, sends `Seek` and returns true without
waiting. The target is not clamped to the track or to the downloaded part. A seek before the
decoder opened the stream is kept: decoding starts at the target. On pause the UI drops the stale
blocks at once, so the decoder fills the queue from the target and `resume()` plays at once.

The decoder seeks with `ma_decoder_seek_to_pcm_frame()`, which can wait for the download to reach
the target; a newer command interrupts it. A seek past the end of the file ends the track at the
target. Until audio from the target plays the position shows the target and the state does not
change.

### `ByteSource`, `StreamBuffer` (`stream_buffer.h`)

```cpp
class ByteSource {
public:
    // Copies the bytes from `offset` that are here now, at most `size`; never waits.
    virtual size_t read(uint64_t offset, void* destination, size_t size) const = 0;
    virtual uint64_t available() const = 0;   // bytes from the start that are here now
    virtual bool complete() const = 0;        // nothing more will come
};

class StreamBuffer : public ByteSource {
public:
    explicit StreamBuffer(std::function<void()> onChange);  // runs after append() and finish()
    void append(const char* data, size_t byteCount);
    void finish();                                          // whole or cut short
};
```

- What the decoder reads. Nothing in it waits: the decoder waits for bytes on its own terms, and
  stops waiting when a command needs it. A file on disk (#30) is another `ByteSource`.
- Each decoder source keeps its own read offset, so a stream opened again (chained again after an
  undo, `playQueuedNow()`) starts at byte 0 by itself.
- `StreamBuffer`: a download as it arrives, the whole file in one growing `std::vector<char>` with
  its own mutex. No size limit. `append` and `finish` come from the UI thread, the reads from the
  decoder thread; `onChange` (the engine passes the decoder's wake-up) runs outside the buffer's
  lock.

### Output device and `QIYAA_AUDIO_BACKEND`

`init()` opens a playback device: f32, 2 channels, `sampleRate = 0` (the device's native rate),
miniaudio's conservative performance profile (default period 100 ms instead of 10 ms,
`MA_DEFAULT_PERIOD_SIZE_IN_MILLISECONDS_CONSERVATIVE`; the backend may choose another size). It
tries the candidate backends one by one, each with its own `ma_context` holding only that backend,
and keeps the first whose device opens:

| Platform | Candidates, in order |
|---|---|
| Linux, FreeBSD | PulseAudio (PipeWire answers through its PulseAudio server), ALSA, Null. No JACK: it writes errors to stderr when no JACK server runs |
| Windows | WASAPI, DirectSound, WinMM, Null |
| anything else (macOS, other BSDs) | Core Audio, Null |

`QIYAA_AUDIO_BACKEND` replaces the list with one backend. Its value is compared with
`ma_get_backend_name()` of every miniaudio backend, case-insensitively and with spaces removed on
both sides: `wasapi`, `directsound`, `winmm`, `coreaudio`, `sndio`, `audio(4)`, `oss`,
`pulseaudio`, `alsa`, `jack`, `aaudio`, `opensl|es`, `webaudio`, `custom`, `null`. `null` plays
nothing but runs in real time, so positions advance; the tests use it. An unknown name logs
`QIYAA_AUDIO_BACKEND=<value>: no such audio backend, using the default ones` (`qWarning`) and keeps
the default list. A known name whose backend does not open fails `init()`; there is no Null
fallback then.

After a device opens, `init()` sets the equalizer's sample rate, allocates the block queue and
starts the decoder thread. It does not start the device: `beginStream()` and `playQueuedNow()` do.
A failure is returned in `InitResult::message`: `no audio output device opens (tried PulseAudio,
ALSA, Null)` (with the real names). The destructor uninitialises the device, stops the decoder
thread, then uninitialises the context.

**Traps:**
- Nothing happens without `poll()`: no `trackFinished`, no `trackAdvanced`, no decode error, and
  `Buffering` never becomes `Playing`.
- `trackFinished` does not stop anything: the state stays `Playing` and the device keeps running
  (silence) until the caller calls `beginStream()`, `playQueuedNow()` or `stop()`.
- `trackFinished` comes once per start or seek: a seek after it plays the track again and reports
  its end again.
- `Buffering` covers only the start of a stream, and a `resume()` before audio is ready. An
  underrun later, or a seek waiting for the download, is silence with the state `Playing`.
- A stream is chained only when completely downloaded, and `complete()` is also true after
  `failData()`: a failed queued download is chained and plays up to its last byte.
- While more may come, the mp3 decoder keeps 16 KB in reserve: of a stalled download the last
  16 KB that arrived (0.7 s at 192 kbps) cannot be decoded, and a seek into them waits for bytes.
- An mp3 that starts with a LAME "Info" or "Xing" frame ends at the length that frame states, even
  when more frames follow.
- After `seek()` or `stop()`, the audio already handed to the device plays out: one period, up to
  100 ms with the conservative profile. So does one period of a cleared stream the callback had
  gone on into.
- `clearQueued()` on a chained stream ends the current track with `trackFinished`; it never
  produces `trackAdvanced`, and a stream queued after it does not chain until a seek or a start.
- Between the callback going on into the queued stream and the next `poll()`, `currentStream()` is
  still the old id and the position keeps the old track's last value.
- An undecodable current stream can be reported only when its download finishes: the MP3 attempt
  reads on looking for a frame, and reads wait for the download.
- While the queue is full, which is most of the time, the decoder thread wakes every 10 ms to look
  for space: the callback wakes no one.
- The default lists end with Null: when no real backend opens, `init()` succeeds without sound.
  Check `backendName()`.

## `equalizer.h` — `EqualizerDsp`

```cpp
inline constexpr int kEqBands = 10;
inline constexpr std::array<double, kEqBands> kEqBandHz = {60,   170,  310,   600,   1000,
                                                           3000, 6000, 12'000, 14'000, 16'000};
inline constexpr double kEqMaxDb = 12.0;

struct EqSettings {
    bool enabled = true;
    double preampDb = 0.0;                   // clamped to ±kEqMaxDb when coefficients are computed
    std::array<double, kEqBands> bandsDb{};  // same

    bool operator==(const EqSettings&) const = default;
};

class EqualizerDsp {
public:
    EqualizerDsp();                                   // 44100 Hz, flat
    void setSampleRate(uint32_t rate);                // 0 means 44100; only while process() cannot run
    void publish(const EqSettings& settings);         // UI thread
    void process(std::span<float> stereoFrames); // audio thread: interleaved stereo, in place
    static double ResponseDb(const EqSettings& settings, double hz, double sampleRate);
};
```

- The filter: preamp gain 10^(dB/20), then 10 peaking biquads per channel from the RBJ audio EQ
  cookbook, Q = 1.2 for every band (roughly an octave wide, so neighbouring bands overlap), run in
  transposed direct form II on `float`. A band closer than 0.05 dB to 0, or at or above 0.49 × the
  sample rate, is skipped (at 22 050 Hz: the 12, 14 and 16 kHz bands). Filter state below 1e-15 is
  flushed to 0 after each block (denormals).
- `publish()` computes the coefficients on the calling thread and hands them over through a triple
  buffer: the writer owns slot `back`, the reader owns slot `front`, and the atomic `middle` holds
  the third slot's index with bit 2 (`kDirty`) set when it is newer than `front`. `publish()`
  exchanges `back` into `middle`; `process()` swaps `middle` into `front` at the start of a block
  when it is dirty. Neither side waits; several publishes between two blocks leave only the last.
- `setSampleRate()` recomputes the last published settings at the new rate and swaps them into
  `front` itself, acting as writer and reader at once. The engine calls it once, in `init()`,
  before the device is ever started; the rate is fixed for the device's lifetime. Settings
  published before `init()` survive it.
- `ResponseDb()` evaluates the magnitude of the product of the float coefficients at `hz`; 0 when
  disabled. Only `dsp_test` calls it.

**Traps:**
- `publish()` is single-writer. Two threads calling it corrupt `back`.
- There is no limiter: preamp and a band together can boost by 24 dB. Samples leave `process()`
  above 1.0 and are only clamped after the volume, in the engine's callback.
- `enabled = false` makes `process()` return before touching anything, so the filter state of the
  moment is kept and re-enabling resumes from it. A band that becomes flat, instead, has its state
  zeroed at once and restarts from rest when it becomes active again.
- The filter state is not reset by seeks or track changes.

## `eq_presets.h` — presets and `.eqf`

```cpp
struct EqPreset {
    QString name;
    EqSettings settings;
};

double EqfToDb(int value);   // Winamp scale 1..64 (clamped) to dB; 33 is exactly 0 dB
int DbToEqf(double db);      // dB to 1..64, rounded to nearest

QList<EqPreset> ParseEqf(const QByteArray& data);   // throws Audio::Error
QByteArray WriteEqf(const QList<EqPreset>& presets);

QList<EqPreset> BuiltinEqPresets();   // 17 presets, all with preamp 0 dB
```

- The scale: dB = (value − 1) / 63 × 24 − 12, so 1 is −12 dB and 64 is +12 dB, except 33,
  Winamp's centre notch, which is exactly 0 dB (the line gives +0.19 dB). `DbToEqf(0)` is 33.
- `BuiltinEqPresets()` is a port of webamp's `presets/builtin.json` (MIT), stored in the 1..64
  scale: Classical, Club, Dance, Laptop speakers/headphones, Large hall, Party, Pop, Reggae, Rock,
  Soft, Ska, Full Bass, Soft Rock, Full Treble, Full Bass & Treble, Live, Techno.
- `WriteEqf()` writes the layout below: the header, then one 268-byte record per preset, nothing
  after. A name is written in the local 8-bit encoding on Windows when it survives the round trip
  (that is the ANSI code page Winamp used), otherwise in UTF-8, cut to 256 bytes so the 257-byte
  field always ends with a NUL; a cut in UTF-8 never splits a character.
- `ParseEqf()` reads names as UTF-8 when they are valid UTF-8, otherwise as the local 8-bit
  encoding on Windows and as Latin-1 elsewhere, and rounds every value to 0.1 dB.

**Traps:**
- A round trip is lossy: 64 levels over 24 dB are steps of 0.38 dB (`eqfRoundTripKeepsNamesAndLevels` allows 0.25).
- `BuiltinEqPresets()` values are not rounded to 0.1 dB, parsed ones are (value 20 is −4.76 dB
  built in, −4.8 dB after a parse).
- A Winamp preset named in cp1251 reads as Latin-1 mojibake outside Windows.
- The UTF-8 cut may drop one whole character more than needed.
- `ParseEqf()` does not bound its input; the caller does (see [Formats read](#formats-read)).

## `vis_tap.h` — `VisTap`

```cpp
struct VisReadResult {
    uint32_t frames = 0;
    uint32_t cursor = 0;
};

class VisTap {
public:
    static constexpr uint32_t kSize = 4096;   // frames; a power of two

    // Audio thread: interleaved stereo frames.
    void write(std::span<const float> stereoFrames);
    // UI thread; at most kSize samples per channel.
    void read(std::span<float> left, std::span<float> right) const;
    // UI thread.
    VisReadResult readNew(uint32_t cursor, std::span<float> stereo) const;
    uint32_t position() const;                                   // frames written, wraps at 2^32
};
```

- Two arrays of 4096 `std::atomic<float>` (left, right) and a free-running `std::atomic<uint32_t>`
  frame counter; slot = counter & 4095. `write()` stores the samples (relaxed), then the counter
  (release); readers load the counter (acquire) and copy. Nothing is locked, and the arrays never
  move. `std::atomic_ref<float>` over plain floats would do the same, but libc++ has it only from
  LLVM 19, and the macOS build is older.
- `read()` copies the latest min(`left.size()`, `right.size()`, `kSize`) frames, oldest first.
  `readNew()` copies the frames written since `cursor`, at most `kSize` and at most `stereo.size() /
  2` (the newest ones), interleaved into `stereo`, and returns their number in `frames` and the
  current counter in `cursor`, the value to pass next time. The unsigned difference `end - cursor`
  stays correct across the wrap at 2^32.
- In the engine the tap receives what the callback played from the blocks, after the equalizer and
  before volume, balance and clamping. `AudioEngine::readVisSamples()` and `readNewVisSamples()`
  pass through to `read()` and `readNew()`;
  `visCursor()` is `position()`. A reader that wants every sample once (Milkdrop) starts its cursor
  from `visCursor()`, so it does not replay what played while it was off.

**Traps:**
- A copy can mix old and new samples when the writer laps the reader. Each sample is atomic, so
  that is a torn picture, not a data race: ThreadSanitizer stays quiet.
- One callback can write more than 4096 frames (a 100 ms period at 48 kHz is 4800); only the last
  4096 survive. A `readNew()` reader that falls more than 4096 frames behind loses the older ones
  silently.
- The counter stops during pause, underruns, before a seek's audio arrives and after a track ended: `read()`
  keeps returning the last frames, `readNew()` returns 0 frames.
- Samples can exceed ±1 (equalizer boost) and do not follow the volume.

## `error.h`, `miniaudio_impl.c`

`error.h` declares `Audio::Error` (see [Errors](#errors)). `miniaudio_impl.c` is the only place
`MINIAUDIO_IMPLEMENTATION` is defined; including `miniaudio.h` anywhere else gives declarations
only.

## Formats read

### Winamp EQ presets: `.eqf` and `.q1`

A `.q1` library has the same layout as an `.eqf`, with more than one record. Offsets in bytes:

| Offset | Size | Content |
|---|---|---|
| 0 | 27 | ASCII `Winamp EQ library file v1.1`, no terminator |
| 27 | 1 | `0x1A` (^Z) |
| 28 | 3 | ASCII `!--` |
| 31 + 268·k | 257 | name of preset k, NUL-padded; it ends at the first NUL, or is all 257 bytes when there is none |
| 31 + 268·k + 257 | 10 | bands 60 Hz … 16 kHz in `kEqBandHz` order, one byte each |
| 31 + 268·k + 267 | 1 | preamp |

Each value byte `b` stores 64 − value, value 1..64: `b = 0` is +12 dB, `b = 31` is 0 dB, `b = 63`
is −12 dB. The format is the one webamp's `winamp-eqf` reads.

`ParseEqf()` throws `Audio::Error` when:
- the data does not start with the 27 header characters, or is shorter than 31 bytes:
  `not a Winamp EQ file: <n> bytes that do not start with "Winamp EQ library file v1.1"`;
- no complete 268-byte record follows offset 31:
  `Winamp EQ file of <n> bytes holds no preset (one takes 268 bytes after the 31-byte header)`.

It does not check bytes 27–30, ignores a trailing partial record, and clamps value bytes instead of
refusing them: any byte from 64 to 255 reads as −12 dB. It returns ⌊(size − 31) / 268⌋ presets and
does not bound the input; the equalizer window refuses files over 1 MiB before reading them
([src/ui](../ui/README.md)), which allows at most 3912 presets.

## Errors

`Audio::Error` (`std::runtime_error`, no children) is the module's only exception and only
`ParseEqf()` throws it, for the two cases above. The equalizer window catches it and shows `what()`
in the status line.

Everything else is data:
- `init()` returns `InitResult{ok, message}`; see [Output
  device](#output-device-and-qiyaa_audio_backend).
- `beginStream()` without a device returns 0, sets `Stopped` and emits
  `errorOccurred("no audio output device")`.
- A first stream that cannot be decoded: `poll()` calls `stop()` and emits
  `errorOccurred("cannot decode the audio stream: <n> bytes received, not mp3, AAC in MP4, flac or wav")`.
- A queued stream that cannot be decoded is dropped silently: `queuedStream()` returns 0 and the
  current track ends with `trackFinished`.
- A failed download (`failData()`) is not an error for the engine: the stream ends where its data
  ends.
- `queueStream()` and `playQueuedNow()` return 0, `seek()` returns false, when they cannot act.
- An unknown `QIYAA_AUDIO_BACKEND` is a `qWarning`, not a failure.
- miniaudio results inside the decoder thread (seek, read errors) are not reported; a read error
  ends the source like its end does.

The device callback and the decoder thread throw nothing of their own. Nothing catches in the
decoder thread, so an allocation failure there (`std::bad_alloc`) would end the process.

## Not here

- Which track plays next, when the next one is queued (`maybePreload`), the downloads that feed
  `appendData()`, the messages for failed downloads, and the timer that calls `poll()`:
  `Player` in [src/core](../core/README.md).
- Resolving a track id into a download link: [src/yandex](../yandex/README.md).
- Spectrum, oscilloscope and Milkdrop, which read the tap through `readVisSamples()` and
  `readNewVisSamples()`: [src/vis](../vis/README.md) and the main and Milkdrop windows in
  [src/ui](../ui/README.md).
- The equalizer window, its sliders and preset menu, loading and saving `.eqf` files from disk:
  [src/ui](../ui/README.md).
- Calling `init()` (skipped in screenshot mode), saving the equalizer settings, and streaming a
  local file into the engine (`StreamLocalFile` in `offline_sources.cpp`, 64 KiB every 20 ms):
  [src/app](../app/README.md).
- miniaudio itself: `contrib/miniaudio/`, built by the top-level `CMakeLists.txt`. The user-facing
  description of `QIYAA_AUDIO_BACKEND` is in [docs/cli.md](../../docs/cli.md).
