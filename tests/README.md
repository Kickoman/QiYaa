# `tests` — Qt Test suites and the mock HTTP server they share

This folder holds one Qt Test executable per area of QiYaa, the shared fixture
`Tests::MockHttpServer` (`support/`) and the files the suites read (`data/`). The suites drive
the real modules: the audio engine plays through miniaudio's Null output, windows open on Qt's
offscreen platform, the Yandex client talks to a local HTTP server, MPRIS runs over a private
D-Bus session and projectM renders on a virtual X server. No suite talks to Yandex Music, a sound
card or a real display. The folder holds no product code: what the suites test lives in `src/`
(see **Not here**). Packaging and installers are not tested here (`packaging/`, CI), and neither
is formatting (the clang-format check in `.github/workflows/ci.yml`).

## Files

| File | Contains |
|---|---|
| `CMakeLists.txt` | `qiyaa_test_support`, the `qiyaa_add_test` helper, one CTest entry per suite, and `milkdrop_gl_test` |
| `support/mock_http_server.h/.cpp` | `Tests::MockRequest`, `Tests::MockResponse`, `Tests::MockHttpServer` |
| `snap_test.cpp` | `Ui::SnapToOthers`, `SnapWithin`, `ClampInside`, `PickScreen`, `ResolveDragPosition`, and `StackBelow` (which windows follow a shade change of window 0). Pure `QRect` arithmetic, no windows |
| `region_test.cpp` | `Skins::ParseRegionTxt` and `RegionFromPolygons`: sections, several polygons, degenerate and missing points, empty input |
| `yandex_test.cpp` | Track-link signing (`BuildTrackUrl`), picking the best full mp3, `ParseDownloadInfo`, `NormalizeToken` on five input shapes, `ApiClient::ParseTrack` and the cover URL |
| `skin_test.cpp` | The built-in base skin, loading every bundled `:/skins/*.wsz`, and the `Skins::Error` messages |
| `audio_test.cpp` | `Audio::AudioEngine` on the test mp3: streaming in chunks, pause, the decode-error message, seeks during download, the `Core::Player` polling timer, and gapless chaining of a queued stream |
| `dsp_test.cpp` | EQ response and processing, the 17 built-in EQ presets, `.eqf` read/write, `EqualizerWindow::GraphCurve`, the FFT analyzer, spectrum and oscilloscope rendering |
| `library_test.cpp` | `Yandex::Library`/`ApiClient` against the mock server: request shapes and parsing for every source the menu offers, likes, waves, search, device login and wave feedback. Also `Core::Player` track events, preloading, and endless sources |
| `main_window_test.cpp` | One `Ui::MainWindow` on the default offscreen screen: position clamping, dragging and edge snapping, shuffle click, volume slider, ×2 and fractional scale |
| `windows_test.cpp` | The whole `App::Application` window set: docking, detaching, scaling, playlist selection/scroll/resize, EQ sliders, shade modes, login dialog layout, Milkdrop and Now Playing windows, `snapshot()` |
| `screenshots_test.cpp` | Golden screenshots of the main window and the equalizer (see **Golden screenshots**) |
| `mpris_test.cpp` | `Integrations::Mpris` over a session bus, driven by `gdbus` as an external client |
| `milkdrop_test.cpp` | `Vis::MilkdropPresets`, preset switching and black-preset handling in `Ui::MilkdropWindow`; with OpenGL 3.3, projectM rendering, the black-picture detector and fullscreen |
| `data/sine440_3s.mp3` | 3 s of a 440 Hz sine: MPEG-1 Layer III, 64 kbps, 44.1 kHz, stereo, ID3v2.4 tag, 24,494 bytes |
| `data/screen-2560x1440.json` | Offscreen-platform config: one screen `"qhd"`, 2560×1440 at (0, 0), logical DPI 96, device pixel ratio 1 |
| `data/golden/*.png` | The seven expected screenshots |

## Building and running

The top-level `CMakeLists.txt` adds this folder when `QIYAA_BUILD_TESTS` is `ON` (the default)
and Qt6::Test is found. Binaries land in `build/tests/`.

```bash
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure                  # every suite
ctest --test-dir build -R library_test --output-on-failure  # one suite
```

`qiyaa_add_test(<name> <modules...>)` builds `<name>.cpp` into the executable `<name>`. It links
`qiyaa_<module>` for each module, plus `qiyaa_test_support` and `Qt6::Test`, and applies
`qiyaa_target_defaults` (`QT_NO_KEYWORDS`, `-Wall -Wextra -Wshadow`). It registers the test with
CTest and sets its environment:

- `QT_QPA_PLATFORM=offscreen`: windows without a display. The offscreen platform's default is
  one 800×600 screen.
- `QIYAA_AUDIO_BACKEND=null`: the engine opens miniaudio's Null output. It plays nothing and
  needs no sound card, but it consumes audio in real time like a device. Positions, pauses and
  track ends therefore behave as they do on hardware. How the engine reads the variable is
  described in [src/audio](../src/audio/README.md).

CTest sets this environment. When you run a binary yourself, set it too. Qt Test takes function
names as arguments:

```bash
QT_QPA_PLATFORM=offscreen QIYAA_AUDIO_BACKEND=null build/tests/audio_test queuedStreamFollowsWithoutGap
```

| CTest name | Links (`qiyaa_*`) | Runs as |
|---|---|---|
| `snap_test` | ui | plain |
| `region_test` | skins | plain |
| `yandex_test` | yandex | plain |
| `skin_test` | skins | plain |
| `audio_test` | audio core | plain; `QIYAA_TEST_DATA` |
| `main_window_test` | audio core skins ui yandex | plain |
| `library_test` | audio core ui yandex | plain; `QIYAA_TEST_DATA` |
| `dsp_test` | audio skins ui vis | plain |
| `windows_test` | app core ui | 2560×1440 virtual screen (not on Windows) |
| `screenshots_test` | audio core skins ui yandex | plain; `QIYAA_TEST_DATA` |
| `mpris_test` | core integrations, Qt6::DBus | only with `QIYAA_HAVE_MPRIS`; under `dbus-run-session` |
| `milkdrop_test` | audio skins ui vis | only with `QIYAA_HAVE_MILKDROP`; offscreen, so the GL functions skip |
| `milkdrop_gl_test` | the `milkdrop_test` binary | only with `xvfb-run` and Qt's xcb plugin; three GL functions |

`QIYAA_TEST_DATA` is a compile definition that holds the absolute path of `tests/data` in the
**source** tree.

**windows_test** needs the stack of main window, equalizer and playlist to fit on screen at
large scales. On everything but Windows its platform is
`offscreen:configfile=<source>/tests/data/screen-2560x1440.json`. Windows is excluded because
Qt splits platform arguments on `:`, which breaks `C:/...` paths. There `requireBigScreen()`
(available height below 1200 px) skips `scalingKeepsTheStack`, `scalingRoundTripsStayDocked`
and `hiddenWindowFollowsScale`. `hugeScaleKeepsEveryWindowReachable` runs on either screen.

**mpris_test** is built only where MPRIS is built: Linux/BSD with Qt D-Bus. macOS Qt has D-Bus
too, but QiYaa has no MPRIS there. The suite needs a session bus. When `dbus-run-session` is
found, CTest runs it as `dbus-run-session -- mpris_test`, on a private bus. Otherwise it runs
directly. With no session bus it then skips itself in `initTestCase`. On a desktop it registers
`org.mpris.MediaPlayer2.qiyaatest` on your own bus. The external-client functions also need
`gdbus` (glib) on `PATH`, or they skip. CI installs `dbus` and `libglib2.0-bin`.

**milkdrop_gl_test**: projectM needs an OpenGL 3.3 context, and the offscreen platform has
none. With `QIYAA_HAVE_MILKDROP`, `xvfb-run` and the target `Qt6::QXcbIntegrationPlugin`
available, CTest adds a second entry. It runs `rendersWithProjectM`, `blackPictureIsNoticed`
and `fullScreenTakesOverRenderingAndGivesItBack` under `xvfb-run -a -s "-screen 0 1280x1024x24 +extension GLX"` with
`QT_QPA_PLATFORM=xcb`, `LIBGL_ALWAYS_SOFTWARE=1` (Mesa's software renderer),
`QIYAA_AUDIO_BACKEND=null` and `QIYAA_EXPECT_GL=1`. `QIYAA_EXPECT_GL` turns "no OpenGL here"
from a skip into a failure, because on that runner OpenGL must work (but see **Traps**).

**QIYAA_UPDATE_GOLDEN=1** records missing golden images. **QIYAA_TEST_SHOTS=<dir>** saves
pictures into an existing directory; nothing creates it:

- `screenshots_test` saves `<data tag>.png`, only for a comparison that fails.
- `milkdrop_test` saves `milkdrop.png`, pass or fail.
- `windows_test` saves `shaded.png`, `milkdrop-window.png` and `nowplaying.png`, pass or fail.

**tests/data**

- `sine440_3s.mp3` feeds `audio_test` and every track of `library_test`'s preload tests.
  `audio_test` checks 44,100 Hz and 2 channels on it. `rapidSeeksWhileDownloadingLandOnTheLastTarget` joins 13
  copies into a stream of about 39 s, which works because MP3 frames concatenate.
- `screen-2560x1440.json` is used only by `windows_test`.
- `golden/` holds the seven PNGs of `screenshots_test`. Recording writes here, into the source
  tree.

## Dependencies

- `qiyaa_test_support`: a static library from `support/`. It PUBLIC-links Qt6::Core and
  Qt6::Network, and its PUBLIC include directory is `tests/`, so the header is
  `"support/mock_http_server.h"`. Every `qiyaa_add_test` target links it. Only `library_test`
  and `windows_test` use it. `mpris_test` is added by hand and does not link it.
- Each suite links the modules in the table above, plus what those modules link PUBLIC. For
  example, `audio_test` reaches `Yandex::ApiClient` through `qiyaa_core`'s PUBLIC link to
  `qiyaa_yandex`.
- Deliberately not linked: the executable `qiyaa` (`src/app/main.cpp`). `windows_test`
  constructs `App::Application` from `qiyaa_app` instead. No suite links projectM directly;
  `milkdrop_test` reaches it through `qiyaa_vis`. The only test framework is Qt Test; there is no
  doctest or gtest.

Invariants, run from the repository root:

```bash
grep -rn --include='*.cpp' --include='*.h' 'qSleep\|sleep_for\|waitForFinished' tests/   # must print nothing
for f in tests/*_test.cpp; do grep -q "$(basename "$f" .cpp)" tests/CMakeLists.txt || echo "$f"; done   # must print nothing
grep -n 'QT_QPA_PLATFORM=' tests/CMakeLists.txt | grep -v 'QIYAA_AUDIO_BACKEND=null'   # must print nothing
```

1. Suites wait only by spinning the event loop (`QTest::qWait`, `qWaitFor`,
   `QSignalSpy::wait`). The mock server, D-Bus replies and the Player's poll timer all run on
   the test thread, so a blocking wait stalls them.
2. Every suite is registered with CTest.
3. Every test environment selects the Null audio output.

## `support/mock_http_server.h` — `Tests::MockHttpServer`

```cpp
namespace Tests {
struct MockRequest {
    QByteArray method;
    QString path;                            // without the query
    QUrlQuery query;
    QByteArray body;
    QHash<QByteArray, QByteArray> headers;   // lower-case names
    QUrlQuery form() const;                  // body as x-www-form-urlencoded, '+' = space
    QString formValue(const QString& key) const;   // fully decoded
};

struct MockResponse {
    int status = 200;
    QByteArray body;
    int delayMs = 0;
};

class MockHttpServer : public QObject {
public:
    using THandler = std::function<MockResponse(const MockRequest&)>;
    MockHttpServer();                                  // listens on 127.0.0.1, any free port
    QString baseUrl() const;                           // "http://127.0.0.1:<port>"
    void on(const QByteArray& method, const QString& path, THandler handler);
    void onPrefix(const QByteArray& method, const QString& prefix, THandler handler);
    void json(const QByteArray& method, const QString& path, const QByteArray& body, int status = 200);
    void result(const QByteArray& method, const QString& path, const QByteArray& resultJson);
    const QList<MockRequest>& requests() const;        // in arrival order
    const MockRequest* last(const QString& path) const;   // nullptr when none
};
}  // namespace Tests
```

- The server serves from the event loop of the thread that created it. A request is read and
  answered only while the test spins that loop.
- A request is recorded once its headers and `Content-Length` bytes of body have arrived. This
  happens before its handler runs, so requests answered 404 are recorded too. `last(path)` is
  the most recent request to that path; the query is ignored.
- Routing tries these in order:
  1. The exact route, method plus path without the query.
  2. The first registered prefix route whose method matches and whose prefix starts the path.
  3. Otherwise `404` with body `{"error":"not found"}`.
- `on` for a method and path that already have a route replaces the handler. That is how a
  suite points a path at new data in a later test function.
- `json` answers a fixed body and status. `result` wraps the body in the Yandex Music API
  envelope `{"invocationInfo":{},"result":<resultJson>}`. Use `json` for endpoints that answer
  without the envelope: `/wheel/new`, OAuth `/device/code` and `/token`, the download-info hop
  `/dl…`, and error bodies.
- Every response is `HTTP/1.1 <status> X` with `Content-Type: application/json`, even for mp3
  and PNG bodies, plus `Connection: close` and `Content-Length`. The server then closes the
  connection, so each connection carries one request.
- `delayMs` delays the reply with a timer on the socket. If the client has gone meanwhile,
  nothing is written.

**Traps:**
- `onPrefix` never replaces. A second registration of the same prefix is appended, and the first
  one keeps answering. `library_test` registers `/get-mp3/` in each preload test; this works only
  because both handlers serve the same mp3.
- Exact routes shadow prefix routes.
- The pointer from `last()` points into `requests()`. It stays valid until the next request is
  recorded, that is, until the test spins the event loop again. Copy what you need before
  waiting.
- A header sent twice keeps its last value. Request bodies must carry `Content-Length`; chunked
  bodies are not read. `QNetworkAccessManager` sends `Content-Length` for byte-array bodies.
- HTTP only. Yandex track links are always `https://`, so `library_test`'s
  `LocalNetworkAccessManager` (a `QNetworkAccessManager`) rewrites `https` URLs with host
  `127.0.0.1` to `http`. The `/get-mp3/…` link then reaches this server.
- It is a `QObject` without `Q_OBJECT`, so it has no signals. Wait on the client's signals or
  callbacks instead.

## Golden screenshots

`screenshots_test` pins how the built-in base skin (`Skins::Skin::BuiltinBase()`) draws two
windows. Each data row is compared with `data/golden/<data tag>.png`:

| Image | Window | Scale | Shaded | Size |
|---|---|---|---|---|
| `main-x1.png` | `Ui::MainWindow` | 1 | no | 275×116 |
| `main-x2.png` | `Ui::MainWindow` | 2 | no | 550×232 |
| `main-shaded-x1.png` | `Ui::MainWindow` | 1 | yes | 275×14 |
| `main-shaded-x2.png` | `Ui::MainWindow` | 2 | yes | 550×28 |
| `equalizer-x1.png` | `Ui::EqualizerWindow` | 1 | no | 275×116 |
| `equalizer-x2.png` | `Ui::EqualizerWindow` | 2 | no | 550×232 |
| `equalizer-shaded-x1.png` | `Ui::EqualizerWindow` | 1 | yes | 275×14 |

Each window is freshly built. The Player has an empty queue, the engine is never initialised,
and no request is made. `window->grab().toImage()` and the PNG are both converted to ARGB32.
Every pixel must be equal; there is no tolerance. A failure reads
`<tag>: size 550x232, expected 275x116`, or
`<tag>: N pixels differ, first at (x, y): actual #aarrggbb, expected #aarrggbb`, or
`no golden image <path>`.

The images hold only skin bitmaps, with no system fonts and no native widgets. They are drawn at
the integer scales 1 and 2 on the offscreen platform, at device pixel ratio 1. That is why they
are the same on Linux, Windows and macOS. Any of these changes breaks that:

- text drawn with a system `QFont`;
- a native control inside these windows;
- a fractional scale (the sharp-bilinear path) added as a golden row.

To record, run with `QIYAA_UPDATE_GOLDEN=1`. The test then writes the image for each data tag
whose PNG does not exist yet and marks that row skipped (`recorded new golden image <path>`).
Rows whose PNG exists are compared as usual; the test never overwrites an existing file. Commit
the new PNG with the change that adds its row.

**An existing expectation is never re-recorded to make a change pass.** Do not delete a PNG and
record it again to get a change through. When a rendering change is intended, say so in the
change, explain why the pixels move, and replace the image as a separate, reviewed step. Adding
a new row with a new image is fine. See `docs/code-style.md` §6 and `CLAUDE.md`.

## Suite notes

These are facts the function names do not carry. What each module promises is in its own README.

**audio_test.** `initTestCase` skips the whole suite if `engine.init()` fails. It sets volume
0, in case the binary runs outside CTest on a real device, and logs the backend name. All
functions share one engine and each starts a new stream. `describeEngine(advancedCount,
finishedCount)` puts the engine's state into the failure messages of the chaining tests: signals
seen, position, state, current and queued stream ids.

- `streamsInChunksAndPlays` feeds the file like a slow network, 4 KB every 10 ms.
- `rapidSeeksWhileDownloadingLandOnTheLastTarget` feeds 64 KB, then 16 KB after each of 24 seeks (3 rounds of 8
  targets, some past what has been downloaded). A final seek to 12 s must play from [12, 14) s.
- `playerPollsTheEngine`: nobody calls `engine.poll()`, so the `Core::Player`'s own timer must
  notice the end of the track.
- `restartingWhileStreamingDoesNotHang`: five `beginStream()` calls 30 ms apart, mid-download, must not hang
  or crash.

The chaining tests start with two helpers. `startNearEnd(at)` plays the file and seeks to `at`
(2.0–2.9 s). `queueWhole` queues a complete second copy.

- `queuedStreamFollowsWithoutGap` expects `trackAdvanced` and no `trackFinished`. It also
  expects no `stateChanged` at all, so no Stopped or Buffering in between. The new track starts
  under 0.3 s, is seekable, and ends with `trackFinished`.
- `seekingBackUndoesTheChain`: a seek back into track 1 after chaining un-chains. The stream is
  queued again and later follows from its own beginning.
- `undecodableQueuedStreamIsSkipped`: the current track ends with `trackFinished` and the queue
  is cleared. The Player then starts the next track itself.
- `queuedStreamArrivingLateStillChains`: track 1 is fully decoded before the queued data
  arrives. The first 4,000 bytes are too few to start decoding it.
- `partlyDownloadedQueuedStreamIsNotChained`: chaining waits for the whole file. Until then the
  track ends with `trackFinished`, and the Player starts the queued stream with
  `playQueuedNow()`.
- `stopReturnsPromptlyWhileQueuedDataIsMissing` and `clearingAChainedStreamThenRestartingDoesNotHang`:
  `stop()` and `beginStream()` must return within 1 s. The decoder never waits forever for a
  queued stream's data.

**dsp_test.**

- `settingsChangingWhileProcessingKeepsOutputBounded` publishes new settings before each of 100 blocks of 480
  frames. The output must stay finite and below 4.0 in magnitude.
- `presetsLookRight`: "Full Bass" is above +5 dB in band 0 and below −5 dB in band 9.
- The `.eqf` tests pin these facts (the layout itself is in
  [src/audio](../src/audio/README.md)):
  - a 31-byte header;
  - 268 bytes per preset (a 257-byte name and 11 value bytes);
  - a stored byte is `64 − level`;
  - byte 31 reads as exactly 0 dB, and 0 dB is written back as 31;
  - byte 255 clamps to −12 dB instead of reaching the DSP as −85 dB;
  - non-Latin names survive a round trip.
- `GraphCurve` gives 9 × 12 + 1 = 109 samples, one band every 12 px, in graph rows: 0 is
  +12 dB and 9 is 0 dB. The spline also lifts the neighbours of a peak.
- The analyzer at 1024 points gives 513 bins. A 1 kHz sine peaks at bin
  round(1000 / (44100 / 1024)) = 23, within 1.5 dB of 0 dBFS, and bin 400 is below −50 dB.
- Spectrum and oscilloscope must each light more than 10 pixels of a 76×16 image.

**library_test.** The suite shares one server and one `ApiClient` with token `test-token`
aimed at the server. It also shares one `Library`. `initTestCase` answers `/account/status`
with uid 42, and `connectsAccountWithAuthHeader` logs in with it.
`J()` writes JSON with single quotes, because moc can't parse raw string literals. `Result<T>`
collects a `(value, error)` callback, and `wait()` spins for up to 5 s.

- `slowLoadDoesNotReplaceNewerChoice`: the likes answer after 300 ms (`delayMs`). "Моя волна"
  is chosen in the meantime, and the late likes must be ignored.
- `wheelOfWavesReadsAnUnwrappedBodyAndSkipsOtherItems`: `/wheel/new` answers without the `{"result": …}` envelope.
- Wave feedback goes to the session endpoint `/rotor/session/<id>/feedback` first. On an HTTP
  4xx (404 here) it falls back to `/rotor/station/<station>/feedback?batch-id=…`, and that
  session stays on the station endpoint without retrying. On a 5xx there is no fallback,
  because the server may have counted the event. `postsSettled` fires only after the fallback
  request has settled too; quitting the app waits for it. The event's `trackId` is
  `<id>:<albumId>`, and `totalPlayedSeconds` is rounded to 0.1 (12.34 → 12.3).
- The track-event log entries are `"<int(TrackEvent)>:<track id>"`, where 0 is Started,
  1 Finished and 2 Skipped. Playing another index logs Skipped for the current track.
  Replacing the queue does the same.
- `PlaybackStack` is a separate `LocalNetworkAccessManager`, `ApiClient`, `Library`, engine and
  `Player` that really plays (on the Null output). `setUpAudio` serves, for each id,
  `download-info`, then `/dlinfo<id>`, then the mp3 under `/get-mp3/`. It returns false, and the
  test skips, when the mp3 can't be read or the engine can't start.
- `playerPreloadsAndAdvancesSeamlessly`: once track 11 is downloaded, 12 is fetched in the
  background (`preloadedIndex() == 1`). The advance is `trackAdvanced`, never `trackFinished`,
  logged as Started 11, Finished 11, Started 12. The download info for 12 is requested once.
  `next()` then takes the preloaded 13 and logs Skipped 12 and Started 13 before it returns.
- `playerRepreloadsWhenTheQueueChanges`: removing the preloaded track (22) makes 23 the next
  one; 23 is preloaded and follows without a gap.
- `playerAsksEndlessSourceForMore`: the engine is never initialised. With 3 tracks, playing
  index 1 leaves 2, which triggers the request for more.

**main_window_test.** All functions share one window on the default 800×600 screen. Input
events go through `QCoreApplication::sendEvent`, which is synchronous.

- The window cannot be dragged past the right edge, and within 15 px of the left edge it snaps
  to it.
- At scale 2 the window is 550×232. At 1.5 it is 413×174, and clicks map back to skin
  coordinates. `setScale(1.3333)` rounds to 1.35.
- `fractionalScaleSizesTheWindowAndMapsClicksBack` grabs the window at 1.5, the sharp-bilinear path, but checks only the
  image size, not the pixels.

**milkdrop_test.**

- The fixture directories are built by `SimplePreset(zoom)`, a tiny valid preset with a
  waveform, zoom and one per-frame equation and no shaders. The built-in one holds `A-first`,
  `b-second`, `c-third` and a `notes.txt` that must be ignored. The user one holds `mine`.
- The engine is never initialised, so projectM gets silence.
- `oversizedPresetIsNotRead`: a file of 1 MiB + 1 byte is listed, but `data()` is empty.
- `builtInPresetsAreBundled` expects at least 50 presets in `:/milkdrop`, each containing
  `[preset`.
- `windowSwitchesPresets` expects no `view()` before the window is shown, because it creates
  nothing OpenGL until then. `onSwitchRequested(Vis::PresetTransition::Blend)` stands for
  projectM's "time is up".
- `onStaysBlack()` stands for the detector reporting the current preset.
  `manyBlackInARowStopsBlamingPresets` reports 12 in a row, and only 5 are recorded. Black
  presets one after another mean something else is wrong, such as no sound reaching projectM
  or a driver problem.
- `rendersWithProjectM` uses `Geometric - RetroTrilogy(Final)` from `:/milkdrop`, which has
  warp and composite shaders and is bright even without sound. It checks, in order:
  1. more than 10 frames within 15 s;
  2. more than 50 colours and more than half of the samples lit, sampling every 4th pixel,
     where R+G+B > 30 counts as lit;
  3. a captured image of view size × device pixel ratio;
  4. an opaque centre pixel;
  5. that hiding the window stops rendering.
- `blackPictureIsNoticed`: `black.milk` draws nothing (decay 0, no wave, borders or motion
  vectors) and must raise `staysBlack`. `white.milk` has a 0.5-wide white outer border and must
  raise `drawsPicture`.
- `fullScreenTakesOverRenderingAndGivesItBack`: in fullscreen the windowed view stops rendering, because the fullscreen
  view renders instead. The final `qWait(100)` lets the deleted fullscreen view go.

**mpris_test.** The suite shares these fixtures:

- an `ApiClient` aimed at `http://127.0.0.1:9`, where nothing listens, so link requests fail
  fast;
- a Player with three tracks ("Песня 1…3" by "Кино", 200 s each);
- a `CoverCache` in a temporary directory;
- `MediaControls` hooks: a volume of 0..100 that starts at 50, a `setVolume` that emits
  `volumeChanged` only on a change (as `MainWindow::setVolume` does, which the app wires to
  `volumeChanged`), and a raise counter.

The service is `org.mpris.MediaPlayer2.qiyaatest` at `/org/mpris/MediaPlayer2`. `ChangeSink`
collects `org.freedesktop.DBus.Properties.PropertiesChanged`.

- `mpris:length` is in microseconds: 200,000 ms gives 200,000,000.
- D-Bus `Volume` 0.3 arrives at the hook as 30.
- While stopped, `CanSeek` is false and `Seek` must not act as "past the end = next".
- `appSideChangesAreAnnounced`: changes made in the app, not over D-Bus, must reach clients too.
- `secondInstanceGetsItsOwnName` stands for a second copy of the app with a second named bus
  connection. Its service name is `<name>.instance…`.

**windows_test.** Each function gets a fresh `App::Application` from `init()`, torn down in
`cleanup()`. Its options are `offline`, `audio = false`, `readOnlySettings` and
`mediaIntegration = false`: no Yandex, no audio device, no settings written, no MPRIS or SMTC.

- The default stack is the main window, then the equalizer below it, then the playlist, at
  275×116, 275×116 and 275×232. `snapshot()` is 275×464.
- `Drag(widget, local, delta)` sends a press, one move and a release.
- The Milkdrop window is off by default. It opens right of the equalizer, at the main window's
  bottom-right corner. Without `QIYAA_HAVE_MILKDROP`, `milkdropWindow()` is null and
  `setMilkdropVisible` does nothing.
- Now Playing is docked right of the main window. Its cover is a red 64×64 PNG served by a
  local `MockHttpServer` at `/cover/400x400`.
- `loginDialogFitsItsText` points OAuth at `http://127.0.0.1:1`, so the device flow fails fast
  with a long "… не удался …" message. At 150 px height, the size GNOME's window manager
  squeezed it to, every visible label must still fit.

## Errors

The suites check both kinds of failure the project has (see `CLAUDE.md`).

- **Exceptions**, from synchronous loaders only:
  - `Audio::Error` from `Audio::ParseEqf`: `"not an eqf"` gives a message that names
    "10 bytes", and a header-only file also throws.
  - `Skins::Error` from `Skins::Skin::LoadWsz` and `LoadFile`: the message starts with the path
    and contains `not a zip archive (20 bytes)`. A missing file throws too.
- **Data**, everything else:
  - `AudioEngine::InitResult {ok, message}`;
  - `AudioEngine::errorOccurred(message)` for undecodable input (the message contains
    "20000 bytes");
  - the error string in `Library` callbacks, which carries the HTTP status and the server's
    message ("401", "Token expired");
  - `DeviceLogin::failed` with the server's `error_description`;
  - an empty `std::optional` from `PickBestVariant` and `ParseDownloadInfo`;
  - an empty `QString` from `NormalizeToken`;
  - an empty `MilkdropPresets::data()` for an oversized file;
  - the `failure()` strings of `MilkdropWindow` and `MilkdropView` when there is no OpenGL.

## Traps

**Timing.** Several suites run in real time, on the Null audio output, over localhost HTTP,
D-Bus, and OpenGL on a software renderer. This is how they stay stable:

- **The Null audio output everywhere.** The gapless test failed once on the macOS runner, the
  only CI machine where the engine got a real device (Core Audio in a VM). Since then CTest
  sets `QIYAA_AUDIO_BACKEND=null` for every test. The `ENVIRONMENT` test property *replaces*
  the whole list. Any new `set_tests_properties(… ENVIRONMENT …)` must therefore repeat
  `QT_QPA_PLATFORM` and `QIYAA_AUDIO_BACKEND=null`, as `windows_test`, `mpris_test` and
  `milkdrop_gl_test` do (third invariant above).
- **Wait for a condition, not for a time.** The suites wait with `pumpUntil` (`audio_test`,
  which calls `engine.poll()` every 20 ms), `QTest::qWaitFor` and `QSignalSpy::wait`. The
  timeouts are 3–5 s, and 15–20 s for OpenGL. A timeout is the ceiling for a slow machine, not
  the expected time. After a pump, the assertion checks the condition again, so a timeout fails
  with the engine's state in the message.
- **The fixed waits that remain:**
  - `audio_test`'s `qWait(150)` gives track 1 time to be decoded to its end and track 2 time to
    be chained. A machine too slow for that still passes `seekingBackUndoesTheChain` and the
    two "clearing a chained stream" tests; they then exercise the unchained path instead.
  - `library_test`'s `qWait(600)` in `slowLoadDoesNotReplaceNewerChoice` waits for a reply
    delayed by 300 ms. If the reply comes later still, the test passes without having tested
    the race.
  - The one wait that can fail on a very slow machine is in the two `library_test` preload
    tests. They wait for `preloadedIndex()`, then `qWait(300)`, then seek to 2.4 s. The 24 KB
    preload has those 300 ms plus the remaining 0.6 s of track to finish over localhost. A
    stream that is not complete is not chained (see `partlyDownloadedQueuedStreamIsNotChained`),
    and then `trackAdvanced` never comes.
- **Wall-clock asserts** are loose. `stop()` and `beginStream()` must return within 1,000 ms;
  a real hang takes far longer. `pauseStopsTheClock` compares the position exactly, because
  while paused the clock must not move at all.
- **The 1000-switch Milkdrop test** is about probability, not time. The old shuffle retried a
  random pick a few times, then fell back to any preset. With 1 of 4 presets black, that picked
  the black one about once in 240 switches. At 30 switches the test failed only now and then;
  at 1000, such a bug fails almost every run. Do not lower the count. The "all others black"
  case runs both in order and shuffled; "all black" runs in order only.
- **Milkdrop rendering takes seconds to appear.** A preset loaded onto a black canvas builds
  its picture through feedback, and more slowly on Mesa's software renderer. The test therefore
  captures every 0.5 s for up to 20 s, and logs what it saw
  (`after N ms: C colours, L of S lit`) and the GL renderer.
- **Frame capture.** The frame comes from `MilkdropView::captureNextFrame()`, which reads it
  right after drawing. `QOpenGLWindow::grabFramebuffer()` reads the back buffer after the swap,
  which is undefined with `NoPartialUpdate`. On Xvfb/Mesa it came back as one flat colour.
- **Black-picture detector timing.** `blackPictureIsNoticed` shortens the detector with
  `setBlackWatchTiming(300, 150, 3)` (grace ms, interval ms, checks). The production defaults
  would take several seconds per preset ([src/vis](../src/vis/README.md)).
- **MPRIS calls must not block.** `gdbusCall` starts `gdbus` with `QProcess` and spins the
  event loop until it exits. The test process itself serves the D-Bus call, so a blocking wait
  would deadlock until the timeout.

**Order and environment:**

- **Shared state in `library_test`.** Later functions rely on earlier ones:
  - `connectsAccountWithAuthHeader` logs in as uid 42, which the `/users/42/…` routes need;
  - the `/tracks/` route from `likedTracksFetchesMetadataAndRemembersLikes` also answers
    `playlistWithoutEmbeddedTracksFetchesByIds` and `likedArtistsAndTheirTopTracksAreParsed`.

  To run one function, name its prerequisites before it. Qt Test runs named functions in the
  order given.
- **Shared state in `mpris_test`.** `appSideChangesAreAnnounced` waits for `Shuffle` to change
  to false. `Player::setShuffle` announces only real changes, so this needs
  `writablePropertiesReachThePlayerAndAreAnnounced` to have switched shuffle on first. That in turn needs `gdbus`, so with
  no `gdbus` or run alone, it times out after 3 s.
- **`QIYAA_EXPECT_GL` is not honoured everywhere.** `rendersWithProjectM` fails when the view
  is missing or never gets ready. `blackPictureIsNoticed` fails only when the view is missing.
  `fullScreenTakesOverRenderingAndGivesItBack` never checks the variable. `milkdrop_gl_test` can therefore pass with
  skips; read the ctest output for `SKIP`.
- **Synthetic input uses skin coordinates.** Examples: Shuffle at (184, 96); the volume slider
  from x = 107; EQ band 60 Hz at x = 78 with 51 px of travel; the playlist's first row at
  y = 20 + 3 + 6. A change to the skin sprite layout moves them. A move event's local
  coordinates are relative to where the window was at the press; the window follows the global
  position.
- **`QTEST_MAIN` or `QTEST_GUILESS_MAIN`.** Suites that create widgets or paint use
  `QTEST_MAIN`: dsp, main_window, milkdrop, screenshots, skin and windows. The others use
  `QTEST_GUILESS_MAIN` (`QCoreApplication`), and a widget created there aborts the test.
- **`stopReturnsPromptlyWhileQueuedDataIsMissing` always feeds half the file.** It appends
  `mp3.left(70'000 < mp3.size() ? 70'000 : mp3.size() / 2)`, and the fixture is 24,494 bytes,
  so the 70,000 branch is never taken.

## Not here

- What the suites test, module by module:
  - [src/audio](../src/audio/README.md): the engine, chaining, `.eqf`, `QIYAA_AUDIO_BACKEND`;
  - [src/yandex](../src/yandex/README.md): endpoints, the result envelope, wave feedback, OAuth;
  - [src/core](../src/core/README.md): Player, preload, track events, the cover cache;
  - [src/ui](../src/ui/README.md): windows, docking, snapping, shade, the login dialog;
  - [src/skins](../src/skins/README.md): skin loading and `region.txt`;
  - [src/vis](../src/vis/README.md): the analyzer, visualizers, Milkdrop presets and view;
  - [src/integrations](../src/integrations/README.md): MPRIS;
  - [src/app](../src/app/README.md): `Application::Options` and `snapshot()`.
- Build options (`QIYAA_BUILD_TESTS`, `QIYAA_WITH_MILKDROP`, MPRIS detection): the top-level
  [CMakeLists.txt](../CMakeLists.txt).
- Which packages each CI runner installs, and the clang-format check:
  [.github/workflows/ci.yml](../.github/workflows/ci.yml).
- The rules for tests (naming, fixtures in `tests/support/` in namespace `Tests`, golden
  snapshots): [docs/code-style.md](../docs/code-style.md) §6 and [CLAUDE.md](../CLAUDE.md).
