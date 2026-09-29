# QiYaa: rules for changing this repository

The shared code style is [docs/code-style.md](docs/code-style.md). It applies here, except for the
points below, which exist because QiYaa is a Qt GUI application rather than a command-line tool.
How the modules fit together is in [docs/architecture.md](docs/architecture.md); each module's
README is its file-level reference.

## Where QiYaa departs from the shared style

- **Qt decides some names.** Overrides of Qt virtuals (`paintEvent`), signals and slots are
  methods, so they are `camelCase` like any method. Use `Q_SIGNALS`, `Q_SLOTS` and `Q_EMIT`,
  never the lowercase macros. D-Bus adaptor slots are named after the MPRIS methods they export
  (`PlayPause`), and Qt Test data functions end in `_data`.
- **Names: the abbreviations QiYaa counts as established.** Besides `id` and `url`: `eq`/`Eq`
  (equalizer), `vis`/`Vis` (visualization, also a module name), `db` (decibels), `hz`, `ms`,
  `kbps`, `fps`, `pcm`, `fft`, `gl` (a `QOpenGLFunctions*`), Qt's `pos` and `event` in overrides,
  Winamp's file names (`gen`, `pledit`, `viscolor`, `numbersEx` for `nums_ex.bmp`), and fields
  that mirror a wire format (`DownloadInfo::ts`, `DownloadInfo::s`). Numeric kernels ported from a
  named source keep that source's formula letters: the natural spline (webamp's `spline.js`), the
  RBJ cookbook biquads and the FFT.
- **Include root is `src/`.** Project headers are written from there (`"audio/audio_engine.h"`),
  tests from `tests/` (`"support/mock_http_server.h"`).
- **Includes.** A `.cpp` may rely on what its own header includes, and an override does not
  include the event type its Qt base class already declares. Otherwise include what you use.
  clang-tidy's `misc-include-cleaner` is a good check, but it maps Qt macros (`QStringLiteral`,
  `QVERIFY`) to internal headers; take the public header (`<QString>`, `<QTest>`) instead.
- **Tests use Qt Test, not doctest.** The tests need Qt's event loop, `QSignalSpy` and windows on
  the offscreen platform. Test functions are `camelCase` sentences about behaviour
  (`queuedStreamFollowsWithoutGap`).
- **Golden snapshots are screenshots**, not CLI output: `tests/data/golden/*.png`, compared pixel
  by pixel. `QIYAA_UPDATE_GOLDEN=1` records images that do not exist yet and never overwrites an
  existing one. `QIYAA_TEST_SHOTS=<dir>` saves the actual image of a failing comparison.
- **Exceptions only in synchronous loaders**: `Skins::Error` from `Skin::LoadWsz`/`LoadFile` and
  `Audio::Error` from `ParseEqf`, caught by the module that calls them (`src/app`, `src/ui`) and
  turned into a message there. An exception never crosses Qt's event loop: not out of a slot, a
  signal handler, a lambda connected to a signal or an audio callback. Network and playback
  failures are data: a signal, a callback argument or a struct with the message.
- **No command functions.** There is no CLI; `main` parses the options into
  `App::Application::Options` and starts the application.
- **Logging instead of output streams.** Nothing takes a `std::ostream&`: diagnostics go through
  `qInfo` and `qWarning`, and user-facing messages through signals to the windows. The one direct
  write to stderr is `main`'s internal-error line.
- **No formats of our own.** Foreign files (`.wsz` and the bitmaps in it, `.eqf`, `.milk`, the
  token file) are read with every size bounded before anything is allocated. Downloads are not
  capped: a track is held whole in memory as its bytes arrive, and a cover is read whole.
- **clang-format is the authority on layout** (`.clang-format`, clang-format 18; CI fails on any
  difference). It keeps the opening brace of a constructor body on the line of the last
  initializer; an empty body is still `{ }`.
- **Language.** Code, comments, module READMEs and the developer documents
  (`docs/architecture.md`, `docs/code-style.md`, this file) are in English. The top-level README
  and the rest of `docs/` are for users and are in Russian.

## Behaviour lives in `spec/`

`spec/` is a submodule, [Kickoman/QiYaa-spec](https://github.com/Kickoman/QiYaa-spec), shared with
the Android app. It holds the player scenarios, the API fixtures, the DSP reference vectors and the
parity table; [spec/README.md](spec/README.md) says what is where. Nothing in it is desktop-only.

- **Change the spec before the code.** When a change alters behaviour that the spec describes (or
  should), commit the spec change to QiYaa-spec first. Then bump `spec/` here in the same change as
  the code and tests, and open an issue in Kickoman/QiYaa-android for the other side.
- **Tests name the scenario.** A test that checks a spec scenario names its ID (`WAVE-03`).
- **Features go in the parity table.** A feature that lands here and not on Android, or the
  other way round, gets a row in [spec/parity.md](spec/parity.md) and an issue for the other
  app. Closing that issue sets the row to "yes".
- **Never edit `spec/` only here.** A change inside the submodule that is not pushed to QiYaa-spec
  breaks every other checkout.

## Before you send a change

```bash
git submodule update --init
cmake -S . -B build -G Ninja && cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
git ls-files 'src/*.cpp' 'src/*.h' 'tests/*.cpp' 'tests/*.h' | xargs clang-format --dry-run --Werror
```

Update the README of every module you touch in the same change, and `docs/` when what the user
sees changes.
