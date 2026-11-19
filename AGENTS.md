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
- **Music sources are a group of modules.** `src/sources/` holds only folders and its README is
  their index; `src/sources/<name>` is a module with the namespace `<Name>Source` (`YandexSource`:
  `Yandex` is the API client's) and the library `qiyaa_sources_<name>`. `src/spotify/` is a group
  too: `src/spotify/<part>`, namespace `Spotify<Part>` (`SpotifyProtocol`), library
  `qiyaa_spotify_<part>`. The rule for modules and groups is in
  [docs/architecture.md](docs/architecture.md#modules).
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
  turned into a message there. The vendored QR code generator adds a third:
  `qrcodegen::data_too_long` from `QrCode::encodeText`, which `Ui::JamWindow` catches where it
  makes the code. An exception never crosses Qt's event loop: not out of a slot, a signal handler,
  a lambda connected to a signal or an audio callback. Network and playback failures are data: a
  signal, a callback argument or a struct with the message.
- **No command functions.** There is no CLI; `main` parses the options into
  `App::Application::Options` and starts the application.
- **Logging instead of output streams.** Nothing takes a `std::ostream&`: diagnostics go through
  `qInfo` and `qWarning`, and user-facing messages through signals to the windows. The one direct
  write to stderr is `main`'s internal-error line.
- **No formats of our own.** Foreign files (`.wsz` and the bitmaps in it, `.eqf`, `.milk`, the
  token file) are read with every size bounded before anything is allocated. Downloads are not
  capped: a track is held whole in memory as its bytes arrive, and a cover is read whole.
- **Lines are up to 120 characters**, not the shared 100: Qt and the skin sprites have long names.
- **clang-format is the authority on layout** (`.clang-format`, clang-format 18; CI fails on any
  difference). It keeps the opening brace of a constructor body on the line of the last
  initializer; an empty body is still `{ }`. Arguments go all on one line (the call's or the next)
  or each on its own line, never packed over two. A broken expression is indented by blocks, never
  aligned far to the right under its first operand.
- **Name a value instead of breaking a call chain.** clang-format cannot keep `.toPoint()` on the
  line of a closing parenthesis: a chain that does not fit becomes `settings` / `.value(…)` /
  `.toPoint()` on separate lines, or an object alone on a line before `->call(…)`. Give the inner
  value a name (`const QPoint rightOfMain = …;`), or name a long lambda before passing it. A list of
  `.arg(…)` calls or a JSON path, one call per line, is fine.
- **One conditional operator per expression.** `a ? b : c` is fine; a nested or chained one is not:
  write a `switch` (in a small function that returns the value) or an `if`/`else if` chain.
- **Language.** Code, comments, module READMEs and the developer documents
  (`docs/architecture.md`, `docs/code-style.md`, this file) are in English. The top-level README
  and the rest of `docs/` are for users and are in Russian.
- **A module README holds contracts, not a second copy of the code.** It says what the module
  promises: the public types and what they guarantee, invariants, which thread may call what,
  formats, the spec's scenarios, and the **Traps**. It does not retell the private implementation:
  the steps of a loop, private fields and who writes them, each step of a method. Ask of a
  paragraph whether someone calling or maintaining the code needs it and could not get it from the
  header and the function in a few minutes. This narrows the shared style's "update the README
  instead": a non-obvious private decision gets its one or two lines in the code (rule 1 allows
  them), and the rest of what private code does is not written down. READMEs move to this rule
  when a change touches them, not in a pass of their own.
- **A README's invariants run in CI.** A command in a `bash` block of a README under `src/` or of
  `tests/README.md` whose line says `# must print nothing` is a check:
  [tools/check_readmes.py](tools/check_readmes.py) runs them all from the repository root and fails
  on any output. It also fails when a folder under `src/` has no README, or a module (a folder with
  files of its own) has no row in the module table of `docs/architecture.md`; it counts only what
  git tracks.
- **Interface texts are English sources in `tr()`** (or `QCoreApplication::translate` outside a
  `QObject`), translated into Belarusian (the default) and Russian in `translations/*.ts`. Never
  write Russian or Belarusian into the code; update the `.ts` files in the same change
  ([translations/README.md](translations/README.md) has the commands and the words). In English,
  Yandex's "волна" is a "vibe": "My Vibe", never "wave".

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
- **Telemetry is spec too.** A new event, field or error code goes into `spec/telemetry` first
  (the jam server refuses anything its schema lacks). Never record free text, a path or a name:
  only codes and counts.
- **Never edit `spec/` only here.** A change inside the submodule that is not pushed to QiYaa-spec
  breaks every other checkout.

## Branches and pull requests

- **A feature gets a branch of its own**, `feature/<name>`, made from `master`. When it takes
  several pull requests (the engine, a client, the source, the menus…), each of them targets the
  feature branch and is reviewed and merged there, never into `master` piece by piece.
- **`master` takes the whole feature at once**: the feature branch is merged when the feature is
  complete (its spec statuses are "yes", docs and translations are in, CI is green), after it has
  been brought up to date with `master`.
- A change that fits one pull request (a fix, a document) goes to `master` through that pull request.
- **A version bump goes straight to `master`, never through a pull request**: the commit
  "Version X.Y.Z for builds from a checkout" (only `QIYAA_VERSION` in `CMakeLists.txt`), pushed to
  `master`, then the tag `vX.Y.Z` on it. The tag's CI builds and checks the release anyway; a pull
  request would only run the same jobs once more on the Actions quota.
- The spec repository is not part of this: a feature's spec commits go to QiYaa-spec's `master`
  first, as above, because the apps pin its commits.

## Before you send a change

```bash
git submodule update --init
cmake -S . -B build -G Ninja && cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
python3 tools/check_readmes.py
git ls-files 'src/*.cpp' 'src/*.h' 'tests/*.cpp' 'tests/*.h' | xargs clang-format --dry-run --Werror
```

Update the README of every module you touch in the same change, and `docs/` when what the user
sees changes.
