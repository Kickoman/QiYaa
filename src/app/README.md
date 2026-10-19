# `src/app` — composition root and the `qiyaa` entry point

`src/app` assembles the running player. `App::Application` creates and owns every long-lived
object (the audio engine, the Yandex API client and library, the player, the jam mode and, in a jam
build, the jam's host session, the cover cache, the media controls, the windows: main, equalizer,
playlist, now-playing and, in a Milkdrop build, Milkdrop, and the controllers below), passes them
their dependencies and connects them; it decides nothing itself. The decisions are the
controllers':

- `App::Settings` keeps `settings.ini`: every key and its default, once;
- `App::WindowSet` places, shows, minimises and scales the windows, one `Spec` each;
- `App::MainMenu` builds the context menus and the keyboard shortcuts;
- `App::Account` finds and saves the Yandex token, logs in and out;
- `App::JamBridge` gives the jam its words and questions (in a jam build);
- `App::TelemetryBridge` is all of the telemetry (in a telemetry build);
- `App::NetworkWatch` tells the player and the jam whether the network is there.

`paths.*` says where QiYaa's own files and the old Yaamp's files live. `offline_sources.*` holds
what `--play-file` and `--demo` play without Yandex. `main.cpp` belongs to the `qiyaa` executable,
not to the library: it picks the Qt platform, parses the command line and runs the application.
Drawing, dragging, snapping, shading and resizing a window are in [src/ui](../ui/README.md); the
queue, playback and wave reports in [src/core](../core/README.md); HTTP, OAuth and the token file
format in [src/yandex](../yandex/README.md); `.wsz` parsing in [src/skins](../skins/README.md);
MPRIS, SMTC and macOS MediaPlayer in [src/integrations](../integrations/README.md).

| File | Contains |
|---|---|
| `application.h/.cpp` | `App::Application` and its `Options`: ownership, wiring, skins, the language, saving, quit |
| `account.h/.cpp` | `App::Account`: the token at the start, login and logout, the likes after a login |
| `jam_bridge.h`, `jam_bridge.cpp`, `jam_off.cpp` | `App::JamBridge`: the jam's words and questions in the app (statuses, "Continue the jam?", the Jam submenu, the server settings); `jam_off.cpp` in a build without the jam |
| `main_menu.h/.cpp` | `App::MainMenu` and `App::TransportKey`: the context menus and the keyboard shortcuts |
| `network_watch.h/.cpp` | `App::NetworkWatch`: the system's view of the network for the `Player` and the jam |
| `telemetry_bridge.h`, `telemetry_bridge.cpp`, `telemetry_off.cpp` | `App::TelemetryBridge`: all of the app's telemetry; `telemetry_off.cpp` in a build without it |
| `window_set.h/.cpp` | `App::WindowSet`: the windows as one set, one `Spec` each: their places, showing and hiding, minimising, scale, "always on top", skin, saved positions, `snapshot()` |
| `settings.h/.cpp` | `App::Settings`: every key of `settings.ini` with its default, typed reads and per-key writes; the only owner of `QSettings` |
| `paths.h/.cpp` | `ConfigDirectory`, `YaampDataDirectories`, `TokenFile`, `YaampTokenFiles`, `SettingsFile`: where settings and tokens live |
| `translations.h/.cpp` | `Language`, `Languages`, `LanguageCode`, `LanguageFromCode`, `LanguageName`, `Translations`: the interface language |
| `offline_sources.h/.cpp` | `StreamLocalFile` (a local file fed to the engine like a download) and `DemoTracks` (nine sample entries) |
| `main.cpp` | `main()` of the `qiyaa` executable: platform choice, command-line options, `--screenshot`, exit codes |

## Dependencies

`qiyaa_app` is built from `account.*`, `application.*`, `main_menu.*`, `network_watch.*`,
`offline_sources.*`, `paths.*`, `settings.*`, `translations.*`, `window_set.*`, the headers
`jam_bridge.h` and `telemetry_bridge.h`, and one of each pair `jam_bridge.cpp`/`jam_off.cpp` and
`telemetry_bridge.cpp`/`telemetry_off.cpp`, as the build has the jam and telemetry or not:

- PUBLIC `qiyaa_audio`, `qiyaa_core`, `qiyaa_skins`, `qiyaa_sources_yandex`, `qiyaa_yandex`:
  `application.h` holds `Audio::AudioEngine`, `Core::Player`, `Core::SourceRegistry`,
  `Core::JamMode`, `Skins::Skin`, `YandexSource::Source`, `YandexSource::Catalog`,
  `Yandex::ApiClient` and `Yandex::Library` by value.
- PRIVATE `qiyaa_integrations`, `qiyaa_ui`, `qiyaa_vis`: the header only forward-declares the
  windows, `Core::CoverCache` and `Integrations::MediaControls`.
- PRIVATE `qiyaa_jam` in a jam build (`QIYAA_HAVE_JAM`), with `jam_bridge.cpp`; a build without
  the jam takes `jam_off.cpp` instead, whose `JamBridge` does nothing. The header forward-declares
  `Jam::HostSession`; `Application` creates it and the jam window under `#ifdef QIYAA_HAVE_JAM`.
- The define `QIYAA_JAM_URL="<the CMake option>"`, the default jam server, in every build:
  `App::Settings` knows the default of `jam/server` whether or not the jam is built.
- PRIVATE `qiyaa_telemetry` in a telemetry build (`QIYAA_HAVE_TELEMETRY`), with
  `telemetry_bridge.cpp`; any other build takes `telemetry_off.cpp`. No header changes with the
  build, and nothing in `src/` tests `QIYAA_HAVE_TELEMETRY` (the checks below).
- Resources `:/icons` (`qiyaa-16.png` … `qiyaa-256.png`, seven sizes), used by `main.cpp` for the
  window icon, and `:/i18n/qiyaa_be.qm`, `qiyaa_en.qm`, `qiyaa_ru.qm`, built from
  [translations](../../translations/README.md) by `qt_add_translations` (Qt LinguistTools is
  required).
- `QT_NO_KEYWORDS` and `-Wall -Wextra -Wshadow` (`/W4 /utf-8` on MSVC), like every target.

The executable `qiyaa` (output name `QiYaa`) is `main.cpp` linked PRIVATE with `qiyaa_app` and
`qiyaa_ui` (`main.cpp` calls `Ui::MainWindow::setStatusText`), with the define
`QIYAA_VERSION="<project version>"`, a `.rc` file from `packaging/windows/qiyaa.rc.in` on Windows
and `resources/icons/qiyaa.icns` on macOS.

`app` deliberately does not link the vendored `qiyaa_miniaudio`, `qiyaa_miniz` or projectM (it
reaches them only through `audio`, `skins` and `vis`), nor Qt D-Bus or WinRT (only through
`integrations`). Nothing links `qiyaa_app` except the executable and, among the tests,
`windows_test`, `settings_test`, `account_test`, `translations_test`, `telemetry_test` and, in a jam
build, `jam_window_test`, `jam_bridge_test` and `jam_e2e_test`.

`app` is the top of the one-way order `audio`, `yandex`, `skins` → `vis`, `core` → `ui`,
`integrations` → `app` → `qiyaa`:

```bash
grep -rln --include='*.h' --include='*.cpp' '#include "app/' src/ | grep -v '^src/app/'   # must print nothing
grep -rln --include='*.h' --include='*.cpp' 'QSettings' src/ | grep -v '^src/app/settings\.'   # must print nothing: only App::Settings keeps settings
grep -rnw --include='*.h' --include='*.cpp' 'throw' src/app/                               # must print nothing: app only catches
grep -rn --include='*.h' --include='*.cpp' 'QIYAA_HAVE_TELEMETRY' src/                      # must print nothing: CMake picks telemetry_bridge.cpp or telemetry_off.cpp
grep -rln --include='*.h' --include='*.cpp' 'Telemetry::' src/app/ | grep -v '^src/app/telemetry_'   # must print nothing: telemetry lives in its bridge
grep -rnE --include='*.cpp' '"[a-z][A-Za-z]*/[a-z][A-Za-z]*"' src/app/ | grep -v '^src/app/settings\.cpp:'   # must print nothing: settings keys are written once, in App::Settings
grep -nE '#include (<(QAction|QActionGroup|QMenu|QMessageBox|QNetworkInformation|QFileDialog|QSettings)>|"(ui/login_dialog|ui/jam_server_dialog|ui/library_menu|yandex/token)\.h")' src/app/application.cpp   # must print nothing: Application builds no menu and asks nothing
```

## `application.h/.cpp` — `App::Application`

```cpp
class Application : public QObject {
public:
    struct Options {
        QString skinOverride;           // --skin; empty = the `skin` setting
        QString settingsFile;           // empty = <ConfigDirectory>/settings.ini
        bool offline = false;           // start() does not look for a token
        bool audio = true;              // false: AudioEngine::init() is never called
        bool readOnlySettings = false;  // settings, covers, Milkdrop presets in a temp dir
        bool mediaIntegration = true;   // MPRIS, SMTC or macOS MediaPlayer
        QNetworkAccessManager* network = nullptr;  // tests: one that reaches a mock server
        std::optional<Language> language;          // this run only; unset: the `language` setting
        QUrl telemetryUrl;  // empty: no telemetry; main.cpp sets it in a telemetry build
    };

    explicit Application(const Options& options, QObject* parent = nullptr);

    void start();

    Ui::MainWindow* mainWindow() const;
    Ui::EqualizerWindow* equalizerWindow() const;
    Ui::PlaylistWindow* playlistWindow() const;
    Ui::NowPlayingWindow* nowPlayingWindow() const;
    Ui::MilkdropWindow* milkdropWindow() const;  // null without QIYAA_HAVE_MILKDROP
    Ui::JamWindow* jamWindow() const;            // null without QIYAA_HAVE_JAM
    Core::CoverCache* covers() const;
    Core::Player* player();
    Audio::AudioEngine* engine();
    Yandex::ApiClient* api();
    Yandex::Library* library();
    YandexSource::Source* yandex();                  // the Yandex source: its quality
    Core::JamMode* jam();
    Jam::HostSession* jamHost() const;                 // null without QIYAA_HAVE_JAM

    WindowSet* windows() const;                        // places, shows, scales the windows (below)
    Account* account() const;                          // login and logout (below)
    TelemetryBridge* telemetry() const;                // never null; does nothing without telemetry

    bool loadSkin(const QString& path);                // false: message in the marquee

    Language language() const;
    void setLanguage(Language language);               // applies now and saves `language`

    void saveState();
    void quit();
Q_SIGNALS:
    void languageChanged(App::Language language);      // setLanguage()
    void skinChanged(const QString& path);             // loadSkin() succeeded
    void skinFailed();                                 // loadSkin() failed
};
```

**Threads and ownership.** Everything runs on the GUI thread: `Application` starts no thread, and
every method must be called from the GUI thread (the audio engine's decoder thread and device
callback stay inside [src/audio](../audio/README.md)). `Application` owns everything it wires:
`App::Settings`, the base and the current `Skins::Skin`, `ApiClient`, `Library`, `AudioEngine`,
`YandexSource::Source`, `Core::SourceRegistry` (the Yandex source under `"yandex"`, added first in
the constructor, before any window), `Player`, `YandexSource::Catalog` and `JamMode` as members; its
own `QNetworkAccessManager` unless `Options::network` lends one; the `Jam::HostSession`,
`CoverCache`, `MediaControls`, the `Mpris`, `MacMediaControls` or `Smtc` object (held as
`std::unique_ptr<QObject>`), the windows (five, and the jam window in a jam build) and the
controllers (`WindowSet`, `Account`, `JamBridge`, `TelemetryBridge`, `MainMenu`) through
`std::unique_ptr`; `NetworkWatch` is a member. The accessors return non-owning pointers, valid until
the `Application` is destroyed.

Member order is load-bearing. Members are constructed in declaration order, and the initialiser list
relies on it: the settings file after the temporary directory, `ApiClient` after the network
manager, the Yandex source after the library, `Player` after the engine and the registry, `JamMode`
after the player. The host session is declared after `JamMode` and destroyed before it. The windows
are declared after everything they point at, and the controllers after the windows, so the
controllers go first, then the windows, while the player, engine and skins they point at still
exist. The `Mpris`/`Smtc`/`MacMediaControls` object dies before the `MediaControls` it wraps, and
`App::Settings` before the temporary directory that may hold its file.

**Construction**, in this order:

1. Opens `App::Settings` on `SettingsFile(settingsFile, temporaryDirectory)`: `settings.ini` in
   a new `QTemporaryDir` when `readOnlySettings`, else `settingsFile`, else
   `<ConfigDirectory>/settings.ini`.
   Then installs the interface language (`Options::language`, else the `language` setting, else
   Belarusian), before any window takes its texts. The `Player` gets the shuffle algorithm, the
   `playback/continueWithVibe` setting and `YandexSource::HistoryWaves` as the vibe that goes on
   after a queue's head (spec CONT-01, CONT-04).
2. Loads the built-in base skin `:/skins/base-2.91.wsz` (`Skins::Skin::BuiltinBase()`). It is also
   the fallback for bitmaps other skins lack.
3. With `audio`, calls `AudioEngine::init()`. On failure it logs `Audio: <message>` and the app
   runs without sound; on success it logs `Audio backend: <name>`.
4. Loads `skinOverride`, or else the `skin` setting. A `Skins::Error` logs
   `Skin: <what>; using the built-in one`.
5. Creates the main, equalizer, playlist and now-playing windows and the cover cache (`<temp
   dir>/covers` when `readOnlySettings`, else the default of [src/core](../core/README.md)). With
   `QIYAA_HAVE_MILKDROP` it also creates the Milkdrop window, with built-in presets from
   `:/milkdrop` and user presets from `<ConfigDirectory>/milkdrop` (`<temp dir>/milkdrop` when
   `readOnlySettings`). The `WindowSet` is created on the main window right after it, and each other
   window joins it with `add()`: it becomes "secondary" (no taskbar button on Windows) and gets its
   saved steps.
6. Connects `YandexSource::Catalog::noticed` to the main window's marquee, worded by
   `Ui::SourceNoticeText`. In a jam build it creates the `Jam::HostSession` (the server and the
   wave feedback from `App::Settings::jam()` at each use, the app's version for `hello`, the session
   file `jam-session.json` next to the settings file, so `readOnlySettings` and a test's
   `settingsFile` keep it in their own folder; `NetworkWatch::back` reconnects it, HOST-26) and the
   jam window (`Ui::JamWindow`, `jamWindow` in the `WindowSet`). Then, in every build, the
   `JamBridge` (below), whose `statusText` goes to the marquee. It is done here, before
   `WindowSet::restore()`, which takes the jam window in.
7. Restores the settings listed in [Settings](#settings) and connects the windows:
   - the equalizer's shade mode shows and sets the main window's volume and balance (`setMixer`,
     `volumeRequested`, `balanceRequested`);
   - the equalizer's `statusText` goes to the main window's marquee;
   - Milkdrop's `statusText` (a preset picked by hand: `Milkdrop: <name>`) goes to the marquee,
     connected after the saved preset is restored;
   - Milkdrop's `transportKey` (transport keys pressed while the visualization has the focus)
     goes to `App::TransportKey`, like the shortcuts;
   - Milkdrop is told whether the engine state is `Playing`.
8. `WindowSet::restore()`: the saved shade state of the main, equalizer and playlist windows, then
   `scale` to every window, then `alwaysOnTop`; its `saveRequested` goes to `saveState()`.
9. With `mediaIntegration`, creates `Integrations::MediaControls` with four hooks: volume, set
   volume, raise (`WindowSet::bringToFront()`) and quit (`quit()`). On top of it, `Mpris` in a build
   with `QIYAA_HAVE_MPRIS`, or `Smtc` in a build with `QIYAA_HAVE_SMTC`, or `MacMediaControls` with
   `QIYAA_HAVE_MAC_MEDIA_CONTROLS` on macOS.
10. Connects `NetworkWatch::onlineChanged` to `Player::setNetworkOnline`, and with audio on and not
    offline starts it (its `back` went to the host session in step 6).
11. Connects `QApplication::aboutToQuit` to `saveState()` followed by `Player::stop()`.
12. Last, the `TelemetryBridge` and the `MainMenu`, which take in every window: the menu puts the
    shortcuts listed below on each of them, and its choices go to `loadSkin()`, `setLanguage()`,
    `saveState()` and `quit()`.

**`start()`** shows the windows (`WindowSet::showAtStart()`). Unless `offline`, it then asks about a
stored jam (`JamBridge::offerStored()`) and looks for a token (`Account::start()`).

**`saveState()`** always writes `volume`, `balance`, `vis/mode`, `time/remaining` and
`equalizer/auto`, then the positions (`WindowSet::savePositions()`, below). It runs on the
`WindowSet`'s `saveRequested` (a window finished a move, a shade changed, a `setScale` with
`ScaleScope::Saved`), on a choice in the Visualization submenu, in `quit()` and on `aboutToQuit`.
The other settings are written the moment they change (see the table).

**`quit()`** runs once; a second call returns at once. It calls `saveState()`, then
`Player::shutDown()` (stops playback, which reports the open track to the wave as skipped, and
ignores anything that would start playback again), hides every window (`WindowSet::hideAll()`), and
then quits the Qt application with a queued call, so that a `quit()` made inside a nested event loop
(a menu) ends the main loop. Before that it calls `TelemetryBridge::finish()`. The queued quit
happens right away if `ApiClient::pendingPosts()` is 0 and the telemetry is not sending, otherwise
when both have settled (`ApiClient::postsSettled`, `TelemetryBridge::sent`) or after 1500 ms,
whichever comes first. `quit()` is reached from the main window's close button (and the window
manager's close), the menu item "Quit QiYaa" and the media controls' quit.

`tests/windows_test.cpp` drives a real `Application` with `offline`, `audio = false`,
`readOnlySettings` and no media integration, on a 2560×1440 offscreen screen (on Windows on Qt's
default offscreen screen, where the scale tests skip themselves).
`tests/jam_e2e_test.cpp` runs one with `offline`, the Null output, a `settingsFile` that points
`jam/server` at a real jam server and `Options::network` at the test's MockHttpServer.

**Traps:**
- Do not reorder the members in `application.h` (see above). A new window is one `WindowSet::Spec`
  in an `add()` after the existing ones (their order is the layout's and the menu's); the menu, the
  keys, the layout and the settings take it from there.
- `Options::offline` only skips the token lookup in `start()`. The menu item "Log in to Yandex
  Music…" still logs in. `readOnlySettings` does not redirect the token file: a login during
  such a run writes the real `<ConfigDirectory>/token`.
- `Options::settingsFile` is set by `jam_e2e_test`, to point `jam/server` at its server, and by
  `telemetry_test`, whose telemetry folder sits next to that file.
- After `--scale`, window positions are not saved for the rest of the run, unless the user picks a
  size from the menu (a `setScale` with `ScaleScope::Saved` clears `transientScale`).
- `saveState()` skips positions while the main window is hidden, which is why `quit()` saves before
  hiding the windows. The `saveState()` on `aboutToQuit` that follows only writes the other keys.
- Only `quit()` waits for pending POSTs. Any other way out (the session ends,
  `QApplication::quit()` called from elsewhere) goes straight to `aboutToQuit` → `Player::stop()`:
  the report for the open track is started, but the process may exit before it leaves.
- The shade state is applied in the constructor, before `start()` places the windows, because the
  saved positions were taken with the shaded heights.
- Settings values are converted with `QVariant::toInt`, `toDouble`, `toBool`, `toPoint`, `toSize`.
  A key that is present but malformed does not fall back to the default (a number becomes 0). Only
  `vis/mode` is clamped here before `static_cast<Ui::MainWindow::VisMode>`; saving uses
  `static_cast<int>`. Volume, balance, scale, resize steps and Milkdrop seconds are clamped
  by the windows. Equalizer values are clamped to ±12 dB by the engine only, so the equalizer
  window draws an out-of-range value as stored.
- A `skin` setting that fails to load is not cleared: the warning repeats at every start until
  another skin is loaded.
- Without a `QNetworkInformation` backend that can tell reachability, `NetworkWatch` says nothing,
  and the `Player`'s retry timer alone ends a wait for the network
  ([src/core](../core/README.md#failures-during-playback)).

### Skins

`loadSkin(path)` loads `path` with the base skin as the fallback. On `Skins::Error` it shows
`Cannot load the skin: <what>` in the marquee, keeps the current skin and returns false. On
success it points every window at the new skin first, and only then replaces (and frees) the old
one; it stores `skin` = `path` and returns true. The Skins submenu passes `:/skins/<file>.wsz` for a
bundled skin, or the path picked in the file dialog.

### Settings

`App::Settings` (below) is the only place in `src/` that reads or writes settings; the table
says who asks it when. "ctor" is the `Application` constructor; "setter" is
`WindowSet::setVisible`.

| Key | Type | Default | Read | Written |
|---|---|---|---|---|
| `skin` | QString: a file path or `:/skins/<file>.wsz` | empty = built-in `base-2.91.wsz` | ctor, unless `--skin` | `loadSkin()` on success |
| `shuffle/algorithm` | QString: `random` or `without-repeats` | `without-repeats` (anything else too) | ctor | on `Player::modesChanged` |
| `playback/quality` | QString: `standard` or `economy` (`Yandex::QualityKey`) | `standard` (anything else too) | ctor | the Quality menu |
| `playback/continueWithVibe` | bool: a new queue goes on with a vibe from its history (spec CONT-01) | true | ctor (`Player::setContinueWithVibeByDefault`) | never: only by hand |
| `language` | QString: `be`, `ru` or `en` | `be` (anything else too) | ctor, unless `Options::language` | `setLanguage()` |
| `scale` | double | 1.0 | ctor | `WindowSet::setScale(…, ScaleScope::Saved)` |
| `alwaysOnTop` | bool | false | `WindowSet::restore()` | `WindowSet::setAlwaysOnTop()` |
| `volume` | int, 0..100 | 75 | ctor | `saveState()` |
| `balance` | int, −100..100 | 0 | ctor | `saveState()` |
| `vis/mode` | int: 0 spectrum, 1 oscilloscope, 2 off | 0 | ctor (clamped to 0..2) | `saveState()` |
| `time/remaining` | bool: show remaining time | false | ctor | `saveState()` |
| `mainWindow/pos` | QPoint | (100, 100) | `WindowSet::showAtStart()` | `WindowSet::savePositions()` |
| `mainWindow/shaded` | bool | false | `WindowSet::restore()` | on `shadeChanged` |
| `equalizer/visible` | bool | true | `WindowSet::showAtStart()`, restoring | setter |
| `equalizer/pos` | QPoint | main position + (0, main height) | `WindowSet::showAtStart()` | `WindowSet::savePositions()` |
| `equalizer/shaded` | bool | false | `WindowSet::restore()` | on `shadeChanged` |
| `equalizer/enabled` | bool | true | ctor | on `EqualizerWindow::settingsChanged` |
| `equalizer/preamp` | double, dB | 0.0 | ctor | on `settingsChanged` |
| `equalizer/bands` | QStringList: 10 numbers in dB, one decimal, in `Audio::kEqBandHz` order (60 Hz first) | empty = all 0.0 | ctor | on `settingsChanged` |
| `equalizer/auto` | bool | false | ctor | `saveState()` |
| `playlist/visible` | bool | true | `WindowSet::showAtStart()`, restoring | setter |
| `playlist/pos` | QPoint | equalizer default + (0, equalizer height) | `WindowSet::showAtStart()` | `WindowSet::savePositions()` |
| `playlist/shaded` | bool | false | `WindowSet::restore()` | on `shadeChanged` |
| `playlist/steps` | QSize: resize steps of 25×29 skin px beyond 275×116 | (0, 4) | `WindowSet::add()` | on `sizeStepsChanged` |
| `nowPlaying/visible` | bool | false | `WindowSet::showAtStart()`, restoring | setter |
| `nowPlaying/pos` | QPoint | main position + (main width, 0) | `WindowSet::showAtStart()` | `WindowSet::savePositions()` |
| `nowPlaying/steps` | QSize: resize steps of 25×29 skin px | (0, 0) | `WindowSet::add()` | on `sizeStepsChanged` |
| `milkdrop/visible` | bool | false | `WindowSet::showAtStart()`, restoring | setter |
| `milkdrop/pos` | QPoint | main position + (main width, main height) | `WindowSet::showAtStart()` | `WindowSet::savePositions()` |
| `milkdrop/steps` | QSize: resize steps of 25×29 skin px | (0, 4) | `WindowSet::add()` | on `sizeStepsChanged` |
| `milkdrop/shuffle` | bool | true | ctor | on `MilkdropWindow::settingsChanged` |
| `milkdrop/locked` | bool: keep the current preset | false | ctor | on `settingsChanged` |
| `milkdrop/seconds` | int, seconds per preset (the window clamps to 5..3600) | 30 | ctor | on `settingsChanged` |
| `milkdrop/preset` | QString: preset name | empty = the window's own pick | ctor | on `settingsChanged` |
| `milkdrop/black` | QStringList: names of blacklisted presets | empty | ctor | on `settingsChanged` |
| `jam/server` | QString: the jam server's address, `https://…` | `QIYAA_JAM_URL` | each time the host session connects; ctor and the dialog for the jam window | "Server settings…" |
| `jam/waveFeedback` | bool: the jam wave learns from skips (HOST-12, HOST-16) | true | when a jam starts | "Server settings…" |
| `jam/shareAudio` | bool: guests may listen along (spec/jam/listen.md) | false | at each `playing` | "Server settings…" |
| `jamWindow/visible` | bool | false | restoring only: the jam window is shown by the menu and by "Continue" | `WindowSet::setVisible()` |
| `jamWindow/pos` | QPoint | main position + (main width, 0) | `WindowSet::showAtStart()` | `WindowSet::savePositions()` |
| `jamWindow/steps` | QSize: resize steps of 25×29 skin px | (1, 8): 300×348 | `WindowSet::add()` | on `sizeStepsChanged` |
| `telemetry/enabled` | bool: "Send statistics" | true | `TelemetryBridge::start()`, the menu | `TelemetryBridge::setEnabled()`, from the menu |
| `telemetry/firstStart` | QString: the ISO date of the first start with telemetry | absent | only whether it exists | `TelemetryBridge::start()`, once |
| `telemetry/machine` | QString: a random UUID, the machine id when the OS has none | absent | `TelemetryBridge::start()` | `TelemetryBridge::start()`, when the OS has no id and the key is absent |

The `milkdrop/*` keys are read and written only in a build with `QIYAA_HAVE_MILKDROP`, the `jam/*`
and `jamWindow/*` keys only in a jam build, the `telemetry/*` keys only in a telemetry build. The
resize steps are clamped to 0..40 per axis by the windows.

## `account.h/.cpp` — `App::Account`

```cpp
class Account : public QObject {
public:
    struct TokenFiles { QString own; QStringList imported; QString ownDirectory; };
    struct Parts { Yandex::ApiClient* api; Yandex::Library* library; Core::Player* player;
                   YandexSource::Catalog* catalog; QNetworkAccessManager* network; QWidget* dialogParent; };
    Account(const Parts& parts, TokenFiles files, QObject* parent = nullptr);
    void setLikesOnHold(std::function<bool()> onHold);   // asked when a login succeeds
    void start();                                      // the token, or the login dialog
    void login();                                      // modal dialog
    void logout();
    void applyToken(const QString& token, bool save);
    void queueLikes();                                 // the likes into an empty queue, when logged in
Q_SIGNALS:
    void statusText(const QString& text);              // for the marquee
    void connected(const QString& displayName);
    void failed();                                     // the account did not connect
    void loggingOut();                                 // before anything is cleared
};
```

`Application` gives it `TokenFile()`, `YaampTokenFiles()` and `ConfigDirectory()`; a test gives it
files of its own, so nothing here touches the real token.

`start()` calls `Yandex::FindToken(own, imported)`, which tries `$QIYAA_TOKEN`, then QiYaa's own
token file, then the old Yaamp `token.json` files. The order, the accepted token formats and the
rule that an empty own file means "logged out" are in [src/yandex](../yandex/README.md).

- No token: `statusText` `Log in: right click → Log in`, and `login()` is queued, so it runs once
  the event loop starts.
- A token: logs `Using Yandex token from <origin>` and calls `applyToken(token, save)`. `save` is
  true only when the origin is neither under `ownDirectory` nor the environment variable (an
  origin starting with `environment`), so a token imported from Yaamp is copied into QiYaa's own
  token file once the account connects.

`applyToken(token, save)` sets the token on `ApiClient`, says `Connecting to Yandex Music…` and
calls `Library::connectAccount`. On error it says `Login failed: <error>` and emits `failed`. On
success it calls `Yandex::SaveToken(own, token)` if `save`, says `Hello, <displayName>!`, emits
`connected`, and, unless the likes are on hold, calls `queueLikes()`:
`YandexSource::Catalog::playLikes(false)` (the liked tracks without starting playback) if the
queue is empty and the library logged in.

`login()` runs `Ui::LoginDialog` modally (`exec()`, a nested event loop) and, on accept, calls
`applyToken(dialog.token(), true)`. `logout()` emits `loggingOut` first, so a jam being created or
on ends while the account still works (the guests search through it, HOST-32), then clears the
play queue, calls `Library::logout()` (which also clears the API token) and
`Yandex::ForgetToken(own)` (leaves an empty token file), and says `You have logged out`.

**Traps:**
- `applyToken` leaves a rejected token set on `ApiClient`; the library just stays logged out. The
  result of `Yandex::SaveToken` is ignored.
- Tests: `tests/account_test.cpp`.

## `jam_bridge.h/.cpp` — `App::JamBridge`

```cpp
class JamBridge : public QObject {
public:
    struct Parts { Jam::HostSession* host; Ui::JamWindow* window; Core::JamMode* jamMode; Core::Player* player;
                   Settings* settings; WindowSet* windows; Account* account;
                   Ui::MainWindow* mainWindow; Ui::PlaylistWindow* playlistWindow; };
    explicit JamBridge(const Parts& parts, QObject* parent = nullptr);   // connects it all
    void offerStored();                                // HOST-23, when a jam is stored
    void addMenu(QMenu* menu);                         // the Jam submenu and a separator
    void showServerDialog();
    QString trackNote(int row) const;                  // HOST-34
Q_SIGNALS:
    void statusText(const QString& text);              // for the marquee
};
```

The jam's part of the app: the words for what the host session says, the questions it asks, and
what the account means to the jam. `Application` creates and owns the host session and the jam
window; the bridge only connects them. A build without the jam compiles `jam_off.cpp` instead of
`jam_bridge.cpp`: the same class, doing nothing, so `Application` calls it in every build.

- **Statuses**, as `statusText`: a refusal by its reason (below); the end of the jam (`The jam is
  over`, `The jam was ended`, `The jam closed while you were away`, `The jam is over: the server
  no longer knows it`); `connectedChanged`, the connection while the jam is on, lost (`No
  connection to the jam server · trying again by itself`) and back (`The jam is connected`,
  HOST-25, HOST-26); `offline` (jam items removed without a connection, HOST-36) as `No
  connection to the jam server`; and the jam window's own `statusText`.
- **The other windows** (HOST-34, HOST-36): the playlist's rows and the marquee get
  `Ui::JamTrackNote` of their entry through `trackNote`, and the playlist repaints on the session's
  `changed`. A pick during a jam replaces the jam's base (HOST-21), and the playlist's remove and
  clear go to the `Player` as ever.
- **The account:** `Account::connected` gives the jam window the host's name; `loggingOut` ends a
  jam being created or on (`cancelCreate()`, `end()`, HOST-32); the likes of a login are on hold
  while a jam is on or the question below is open.
- **"Continue the jam?"** (HOST-23). With a stored jam, `offerStored()` opens a `QMessageBox` "A
  jam was on" / "Continue it? The guests' link stays the same." with "Continue" and "End it". It is
  not modal (`open()`): playback and the windows work while it waits, and a test on the offscreen
  platform does not block. "Continue" calls `continueStored()` and shows the jam window; "End it"
  calls `discardStored()` and `Account::queueLikes()`, which does not ask the hold: the question is
  still open inside its own `finished`. Closing it without an answer keeps the jam stored for the
  next start.
- **The Jam submenu:** "Start…" (no jam) or "Jam window" (a jam is on or being created), which show
  the jam window; "Join…", disabled until Kickoman/QiYaa#16; "End", enabled while a jam is on,
  which asks "End the jam? The guests will see that it is over." in a modal `QMessageBox` and then
  calls `HostSession::end()`; "Server settings…".
- **"Server settings…"** (`showServerDialog()`, also the jam window's `serverSettingsRequested`):
  `Ui::JamServerDialog`, modal, which writes `jam/server`, `jam/waveFeedback` and `jam/shareAudio`
  on Save and tells the jam window the server's host (and a port that is not the default one).

The refusals of the jam server, by reason: `server-full` "The jam server is full, try later",
`rate-limited` "Too many requests, wait a little", `update-required` "Update QiYaa to work with
this jam server", `not-allowed` "The jam server does not allow this", `queue-limit` "The jam queue
is full", `duplicate` "This track is already in the jam queue", `stale` "This track has already
left the queue", any other "The jam server refused". The texts are the Android app's.

Tests: `tests/jam_bridge_test.cpp`; "Continue the jam?" through the whole app in
`tests/windows_test.cpp`.

## `main_menu.h/.cpp` — `App::MainMenu`

```cpp
void TransportKey(Core::Player* player, Audio::AudioEngine* engine, int key);   // Z X C V B, Left, Right

class MainMenu : public QObject {
public:
    struct Parts { Core::Player* player; Audio::AudioEngine* engine; Yandex::Library* library;
                   YandexSource::Catalog* catalog; Core::SourceRegistry* sources; YandexSource::Source* yandex;
                   Settings* settings; WindowSet* windows; Account* account; JamBridge* jam;
                   TelemetryBridge* telemetry; const Translations* translations;
                   Ui::MainWindow* mainWindow; Ui::PlaylistWindow* playlistWindow; };
    explicit MainMenu(const Parts& parts, QObject* parent = nullptr);   // shortcuts, menu requests
    void showMain(QPoint globalPosition, const Core::Track* track);
    void showSources(QPoint globalPosition);
Q_SIGNALS:
    void skinChosen(const QString& path);  void languageChosen(App::Language language);
    void visModeChosen();  void quitChosen();
};
```

The menus and the keys. What the `Application` owns (skins, the language, saving, quitting) the
menu asks for with a signal; the rest it does through the parts. Built last, after every window
joined the `WindowSet`.

**Menus.** `showMain(globalPosition, track)` (a `const Core::Track*`) answers the main window's
options button and right-click, and the playlist's ADD button and right-click. The track is the
playlist row under the cursor for `trackMenuRequested`, and the current track for the other entry
points. `showSources(globalPosition)` answers the main window's eject button with the current
track. Both build a `QMenu` parented to the main window, open it with `popup()` and delete it on
close. The playlist's ADD button and right-click open the full main menu: track rows emit
`PlaylistWindow::trackMenuRequested`; ADD and clicks outside the rows emit
`PlaylistWindow::sourcesMenuRequested`.

The sources menu holds the Yandex items from `Ui::AddLibraryActions` (only "Log in to Yandex Music…"
while logged out; it calls `Account::login()`). The texts here are the English sources; the user
sees them in the interface language. The main menu, top to bottom:

- the same Yandex items;
- in a jam build, Jam (`JamBridge::addMenu`, below);
- every window whose `WindowSet::Spec` has a `menuText`, with its `key`: Equalizer (Alt+G),
  Playlist (Alt+E), Now playing (no shortcut), Milkdrop (Ctrl+Shift+K, only in a Milkdrop build);
  checkable, checked when the window is visible;
- Visualization: Spectrum, Oscilloscope, Off (exclusive; a choice emits `visModeChosen`, which
  saves);
- Shuffle: "Random track" or "Without repeats" (exclusive); chooses the preferred algorithm
  and saves `shuffle/algorithm`. It does not toggle the shuffle button or restart audio.
- "Continue with a vibe" (`continueWithVibe`): checkable, checked when the current queue goes on
  with a vibe from its history (`Player::continuesWithVibe`), disabled when its source has a
  continuation of its own (My Vibe, a station). It switches the current queue only (spec CONT-02),
  never the `playback/continueWithVibe` setting.
- Quality (`qualityMenu`): "Standard" or "Economy: a third of the data" (exclusive); calls
  `YandexSource::Source::setQuality` and saves `playback/quality` (spec/player/quality.md). The
  track that plays goes on; the next one is fetched in the new quality.
- Skins: every `*.wsz` in `:/skins`, sorted by name, shown without the extension; then "Load a
  skin…" (file dialog starting in the home directory, filter `*.wsz *.zip`); a choice emits
  `skinChosen(path)`;
- Size: 100 %, 125 %, 150 %, 175 %, 200 %, 250 %, 300 % (exclusive, checked when the current
  scale is within 1e-6), then "Double size" (Ctrl+D);
- the language: "Мова (Language)" in Belarusian, "Язык (Language)" in Russian, "Language" in
  English, so that a wrong pick can be undone; "Беларуская", "Русский", "English" (exclusive, each
  in its own language) emit `languageChosen`;
- Always on top: checkable, checked from the main window's current flag;
- "Send statistics", only when `TelemetryBridge::available()`;
- "Log out (<displayName>)", only while logged in: `Account::logout()`;
- "Quit QiYaa": `quitChosen`.

**Shortcuts.** The constructor adds one `QAction` per key to every window of the `WindowSet` (the
jam window's fields take the keys while they have the focus). `QAction`'s default context is the
window, so the keys work while any of the player's windows is active. A window's key is its
`Spec::key`; the transport keys, Ctrl+D and Ctrl+W are named once in `main_menu.cpp`. A menu item
shows the same key, so the label and the key that works cannot drift apart.

| Key | Action |
|---|---|
| Z, X, C, V, B | previous, play, pause, stop, next |
| Left, Right | seek 5 s back or forward from `AudioEngine::positionSeconds()` |
| Alt+G | show or hide the equalizer |
| Alt+E | show or hide the playlist |
| Ctrl+D | scale 2.0, or 1.0 when it is already 2.0 (saved) |
| Ctrl+W | shade or unshade the main window |
| Ctrl+Shift+K | show or hide Milkdrop (only in a Milkdrop build) |

## `network_watch.h/.cpp` — `App::NetworkWatch`

`start()` loads a `QNetworkInformation` backend that can tell reachability; without one it does
nothing. It then reports the reachability now and at every change: `onlineChanged` with Online as
true, Disconnected as false and anything else (Local, Site, Unknown) as "cannot tell", and `back()`
at every Online, the first one too.

## `telemetry_bridge.h/.cpp` — `App::TelemetryBridge`

```cpp
class TelemetryBridge : public QObject {
public:
    struct Sources { Application* application; Settings* settings; Account* account; WindowSet* windows;
                     Core::Player* player; Audio::AudioEngine* engine; Yandex::ApiClient* api;
                     Ui::MainWindow* mainWindow; Ui::EqualizerWindow* equalizer;
                     Ui::MilkdropWindow* milkdrop; Jam::HostSession* jamHost; };   // the last two may be null
    struct Config { QUrl url; QNetworkAccessManager* network; QString skinOverride; bool audio; bool audioFailed; };
    TelemetryBridge(const Sources& sources, Config config, QObject* parent = nullptr);
    bool available() const;                            // a telemetry build with a URL
    bool enabled() const;  void setEnabled(bool on);   // telemetry/enabled
    void start();                                      // from Application::start()
    void finish();  bool isSending() const;            // for quit()
    Telemetry::Reporter* reporter() const;             // null while off; for the tests
Q_SIGNALS:
    void sent();                                       // the reporter's batch went
};
```

All of the app's telemetry ([src/telemetry](../telemetry/README.md), spec/telemetry). The bridge
subscribes itself to what it reports, so nothing else in `src/app` knows telemetry.
`telemetry_bridge.cpp` is built only with `QIYAA_WITH_TELEMETRY`; any other build compiles
`telemetry_off.cpp`, whose bridge is never `available()` and does nothing. Inside
`telemetry_bridge.cpp` the subscriptions to the host session and to the Milkdrop window are under
`#ifdef QIYAA_HAVE_JAM` and `#ifdef QIYAA_HAVE_MILKDROP`, since those classes exist only in their
builds.

With a URL and `telemetry/enabled` on (the default), `start()` creates the `Telemetry::Reporter`
in `telemetry/` next to `settings.ini` and records `start`. Its fields are the system's
(`Telemetry::SystemFields`), the state the windows start in, and `firstRun`: the settings were
empty when the app opened them. `telemetry/firstStart` is written and synced at once, so a first
run that gets killed does not make the next one first too. Without a reporter every event is
dropped.

| Event | From |
|---|---|
| `error playback/<kind>` | `Core::Player::failed` |
| `error audio/device`, `audio/undecodable`, `audio/init` | `AudioEngine::errorOccurred`, `streamUndecodable`, a failed `init()` (`Config::audioFailed`, at `start()`) |
| `error api/<kind>` (+ `httpStatus`) | `Yandex::ApiClient::requestFailed` |
| `error login/failed`, `skin/load` | `Account::failed`, `Application::skinFailed` |
| `error milkdrop/<category>` | `Ui::MilkdropWindow::failed` |
| `error jam/<reason>`, `jam/gone`, `jam/expired`, `jam/ended-by-server` | `HostSession::refused`, `ended` |
| `feature vis`, `equalizer`, `skin`, `language`, `milkdrop`, `jam` | `MainWindow::visModeChanged`, the equalizer's `settingsChanged`, `Application::skinChanged`, `Application::languageChanged`, `WindowSet::visibilityChosen` of Milkdrop, `HostSession::activeChanged` |

A skin is `base`, a built-in skin's name or `custom`, never a path. `quit()` calls `finish()`
(`exit`) and waits for the last batch (`sent`) within its 1.5 s, like the plays reports. The menu's
"Send statistics" is shown only when `available()`. Off (`setEnabled(false)`):
`Reporter::forget()` and `ForgetAll` delete what waits. On: `start()` again, as a new session.

Tests: `tests/telemetry_test.cpp`.

## `window_set.h/.cpp` — `App::WindowSet`

```cpp
class WindowSet : public QObject {
public:
    enum class Place { Stacked, RightOfMain, BelowRightOfMain };
    enum class Button { None, Equalizer, Playlist };   // the main window's EQ or PL button
    enum class ScaleScope { Saved, ThisRun };          // ThisRun for --scale
    struct Spec {
        QString group;                                 // the settings group: <group>/pos, /visible…
        Place place = Place::RightOfMain;              // the default place
        bool visibleByDefault = false;
        bool shownAtStart = true;                      // false: only menus, "Continue" and restoring
        bool keepsShade = false;                       // <group>/shaded
        bool raisedWhenShown = false;
        QSize stepsByDefault;                          // invalid: no resize steps
        Button button = Button::None;
        const char* menuText = nullptr;                // QT_TRANSLATE_NOOP("App::MainMenu", …); none: no item
        QKeySequence key;                              // toggles the window; shown in the menu
    };
    WindowSet(Settings* settings, Ui::MainWindow* mainWindow, Spec mainSpec, QObject* parent = nullptr);
    void add(Ui::SkinnedWindow* window, Spec spec);    // secondary, saved steps, close = hide
    void restore();                                    // once, after the last add()
    void showAtStart();
    void setVisible(Ui::SkinnedWindow* window, bool on);   // nothing for a null or unknown window
    void toggle(Ui::SkinnedWindow* window);
    void raise();  void bringToFront();  void hideAll();
    void setScale(double scale, ScaleScope scope = ScaleScope::Saved);
    void setAlwaysOnTop(bool on);  void setSkin(const Skins::Skin* skin);  void savePositions();
    QList<Ui::SkinnedWindow*> all() const;             // the main window first, then the add() order
    const Spec& spec(const Ui::SkinnedWindow* window) const;   // the main window's for an unknown one
    QImage snapshot() const;                           // visible windows, as placed on screen
Q_SIGNALS:
    void visibilityChosen(Ui::SkinnedWindow* window, bool on);   // from setVisible() only
    void saveRequested();
};
```

The windows as one set: a window's defaults are its `Spec`, its keys `App::Settings`'s, and what a
window needs from the app is in `add()` and the `Spec`, so a new window does not touch the code
below. The main window comes with the constructor, not `add()`, and its `Spec` gives only its
group: it always keeps its shade, starts at (100, 100) without a saved place, is shown first, never
hidden by minimising, its close is the app's `quit()`, and it is the first of `all()`.

- **`add()`** makes the window secondary, restores its steps (when `stepsByDefault` is valid) before
  it listens for their changes, and hides it on its own close button (`closeRequested`).
- **`restore()`**, once after the last `add()`, in this order: `moveFinished` of every window to
  `saveRequested`, the saved shade of the main window and of every `keepsShade` window (each
  change written and `saveRequested`), `scale` on every window, then `alwaysOnTop`.
- **`showAtStart()`** shows the main window, then every `shownAtStart` window whose
  `<group>/visible` is true, places them all, sets the EQ and PL buttons to match, and activates the
  main window.

**Layout**, only in `showAtStart()`: each window at its saved `<group>/pos`, or at its place: the
main window at (100, 100); `Stacked` right below the previous stacked window's default (the
equalizer under the main window, the playlist under the equalizer); `RightOfMain` (now-playing, the
jam window); `BelowRightOfMain` at main position + (main width, main height) (Milkdrop). The
defaults use the sizes the windows have at that moment, after shade and scale.
`SkinnedWindow::placeAt` clamps every position to the screens that exist now.

**`setScale(scale, scope)`**:

1. Collects the windows docked to the main window through a chain of touching windows
   (`Ui::ConnectedGroup`, breadth-first, so nearest first). Hidden windows are included, so they
   are in place when shown. It records each one's offset from the main window in skin pixels
   (offset / old scale, rounded).
2. Calls `setScale` on every window. The window clamps the factor to [1, 4], rounds it to 0.05,
   and may move itself back onto the screen, so the main window's position is read only after
   this step.
3. Puts each docked window back at main position + offset × new scale, snapped with
   `Ui::SnapToOthers(frame, placed, 4)` to the windows already placed. Window sizes are rounded one
   by one, and the 4 px snap closes the resulting 1 px gaps.
4. Moves the visible group as a whole into the screen it is on (`Ui::PickScreen`,
   `Ui::ClampInside`). If the group is wider or taller than that screen, it calls
   `ensureVisible()` on every window so each one can still be reached.
5. With `ScaleScope::Saved`, stores `scale` (the factor the main window ended up with) and emits
   `saveRequested`. Without it (`--scale`), it sets `transientScale`, which stops
   `savePositions()` from writing.

Windows not docked to the main window keep their top-left corner and only change size.

**Visibility.** `setVisible` shows or hides the window, calls `ensureVisible()` when showing it
(and `raise()` for `raisedWhenShown`), sets its button, stores `<group>/visible` and emits
`visibilityChosen`. A window's own close button is `setVisible(false)`. Minimising the main window
hides all the others; restoring it shows those whose `<group>/visible` is true, the jam window
too (see the traps), without `ensureVisible()` or the buttons. `raise()`, on the main window's
`activated`, raises the visible windows and then the main one; `bringToFront()`, the media
controls' raise, un-minimises the main window, raises the windows and activates the main one.
`setAlwaysOnTop` sets `Qt::WindowStaysOnTopHint` on every window, then shows again those that were
visible, because changing window flags hides a window.

**`savePositions()`** writes every window's `<group>/pos` only when all three hold: the platform
lets the app place windows (`SkinnedWindow::CanPositionWindows()`, false on native Wayland), the
main window is visible, and no `--scale` override is active.

**`snapshot()`** paints `grab()` of each visible window into an `ARGB32_Premultiplied` image the
size of the bounding rectangle of all visible windows, each at its offset in that rectangle. Gaps
stay transparent. With no visible window the image is null.

**Traps:**
- The jam window is not shown at the start, only by the menu and by "Continue" (HOST-23), but
  restoring from minimise shows it when `jamWindow/visible` is true. So a jam window left open at
  the last quit comes back after a minimise even with no jam on.
- `visibilityChosen` is not emitted by minimising, restoring or the start: telemetry counts what
  the user chose.

## `settings.h/.cpp` — `App::Settings`

```cpp
struct WindowState { std::optional<QPoint> pos; bool visible = false; bool shaded = false; QSize steps; };
struct MainWindowState { int volume = 75; int balance = 0; int visMode = 0; bool remainingTime = false; };
struct MilkdropSettings { bool shuffle = true; bool locked = false; int seconds = 30; QString preset; QStringList black; };
struct JamSettings { QString server; bool waveFeedback = true; bool shareAudio = false; };

class Settings {                                      // not a QObject; owns the QSettings
public:
    explicit Settings(const QString& fileName);       // INI format
    QString directory() const;                        // absolute folder of settings.ini
    bool wasEmpty() const;                            // no key when it was opened: the first run
    // language, skin, scale, alwaysOnTop, shuffleAlgorithm, quality, continueWithVibe (read only),
    // mainWindow, equalizer, equalizerAuto, milkdrop, jam, telemetryEnabled, machine: a getter
    // with the default of the table above, and a setter that writes the key(s) at once
    WindowState window(const QString& group, bool visibleByDefault, QSize stepsByDefault = {}) const;
    void setWindowPos(const QString& group, QPoint pos);   // and setWindowVisible, setWindowShaded,
                                                            // setWindowSteps: one key each
    static QString DefaultJamServer();                // QIYAA_JAM_URL
    bool hasFirstStart() const;
    void setFirstStart(QDate date);                   // written and synced at once
};
```

- Every key and its default is written once, here. The defaults of a window (`visible`, `steps`,
  the place) are the caller's, a key is `<group>/<field>`.
- Reads convert like `QSettings` always did (see the traps in [Settings](#settings)): a present
  but malformed value is not the default. `vis/mode` is clamped to 0..2; `shuffle/algorithm`,
  `playback/quality` and `language` fall back for anything they do not know; `jam/server` is
  trimmed. `WindowState::pos` is empty when the key is absent.
- Writes go key by key, at the moment the value changes, so nothing writes a key the app has
  not chosen yet: a fresh start writes nothing.
- `setMainWindow`, `setEqualizer`, `setMilkdrop` and `setJam` write all their keys.
- Tests: `tests/settings_test.cpp`.

## File format: `settings.ini`

`QSettings::IniFormat`, written by Qt. Keys without a group go under `[General]`; `a/b` is key `b`
in group `[a]`. Points and sizes are written as `@Point(x y)` and `@Size(w h)`, string lists as
comma-separated values, bools as `true`/`false`:

```ini
[General]
skin=:/skins/Vizor1-01.wsz
scale=1.5
volume=75
balance=0

[equalizer]
bands=3.0, 1.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.5, 3.0
pos=@Point(100 274)
visible=true

[playlist]
steps=@Size(0 4)
```

The loader refuses nothing. Unknown keys are ignored (and kept), missing keys take the defaults
from the table, and malformed values are converted as described in the traps above. For
`equalizer/bands`, the first 10 entries are used, and bands without an entry stay at 0.0 dB. The
module reads no other file itself: the token files are read by [src/yandex](../yandex/README.md),
skins by [src/skins](../skins/README.md).

## `translations.h/.cpp`

```cpp
enum class Language { Belarusian, Russian, English };
inline constexpr Language kDefaultLanguage = Language::Belarusian;
const QList<Language>& Languages();                     // in the menu's order
QString LanguageCode(Language);                         // "be", "ru", "en"
std::optional<Language> LanguageFromCode(const QString&);
QString LanguageName(Language);                         // "Беларуская", "Русский", "English"
class Translations {
    bool apply(Language language);                      // false: the app's .qm is missing
    Language language() const;
};
```

`apply` removes the translators it installed before, sets `QLocale::setDefault`, loads
`:/i18n/qiyaa_<code>.qm` (for English only the plural forms) and Qt's `qtbase_<code>.qm` from
`QLibraryInfo::TranslationsPath` when Qt has one (Russian; not Belarusian), and installs them on
the application. Qt then sends `QEvent::LanguageChange` to every widget: the skinned windows draw
their texts again and set their titles in `retranslate()` (see [src/ui](../ui/README.md)); menus
and dialogs take the new texts when they open next. A text already shown in the marquee stays
until the next one. The destructor removes the translators.

**Traps:**
- Install before the windows exist: a text set in a constructor (a window title, a dialog's
  label) is taken in the language of that moment, unless the widget sets it again on
  `LanguageChange`.
- Qt has no Belarusian translation: its standard buttons and its own file dialog stay in English
  there. The app's dialogs set their buttons' texts themselves (`Ui::AskText`, `Ui::AskItem`,
  the jam's questions).

## `paths.h/.cpp`

```cpp
namespace App {
QString ConfigDirectory();            // created if missing
QStringList YaampDataDirectories();    // old Yaamp data folders, may not exist
QString TokenFile();            // <ConfigDirectory>/token
QStringList YaampTokenFiles();  // <dir>/token.json for each YaampDataDirectories() entry, same order
QString SettingsFile(const QString& chosen, const QTemporaryDir* temporaryDirectory);
                                // <temp dir>/settings.ini, else chosen, else <ConfigDirectory>/settings.ini
}
```

`ConfigDirectory()` is `QStandardPaths::GenericConfigLocation` + `/QiYaa`. `YaampDataDirectories()` is where
Electron's `app.getPath('userData')` (`<appData>/<productName>`) put the old Yaamp's data, with the
product name spelled `Yaamp` and then `yaamp`:

| OS | `ConfigDirectory()` | `YaampDataDirectories()` |
|---|---|---|
| Linux, BSD | `$XDG_CONFIG_HOME/QiYaa`, default `~/.config/QiYaa` | `$XDG_CONFIG_HOME/Yaamp`, `…/yaamp`; `~/.config/…` when the variable is empty or unset |
| Windows | `%LOCALAPPDATA%\QiYaa` | `%APPDATA%\Yaamp`, `%APPDATA%\yaamp`; none when `APPDATA` is unset |
| macOS | `~/Library/Preferences/QiYaa` | `~/Library/Application Support/Yaamp`, `…/yaamp` |

What lives in `ConfigDirectory()`: `settings.ini`, `token`, in a Milkdrop build `milkdrop/` (the
user's own presets), and while a jam is on `jam-session.json` (next to `settings.ini`, see
[src/jam](../jam/README.md#session_storeh)). The cover cache is not there: [src/core](../core/README.md) chooses its
place.

**Traps:**
- `ConfigDirectory()` calls `QDir().mkpath()` on every call, `TokenFile()` included, and ignores a
  failure.
- On Windows QiYaa's folder is under Local (`%LOCALAPPDATA%`, Qt's choice), while Yaamp's is under
  Roaming (`%APPDATA%`); on macOS it is `Preferences`, not `Application Support`.
- The "imported" test in `Account::start()` is a string-prefix test against `ConfigDirectory()`. If
  `TokenFile()` ever moves out of `ConfigDirectory()`, QiYaa's own token counts as imported and is
  saved again at every start.
- On case-insensitive file systems (Windows, macOS by default) `Yaamp` and `yaamp` are the same
  folder, so the same `token.json` may be read twice; `FindToken` stops at the first usable one.

## `offline_sources.h/.cpp`

```cpp
namespace App {
void StreamLocalFile(Audio::AudioEngine* engine, const QString& path);
QList<Core::Track> DemoTracks();
}
```

`StreamLocalFile` imitates a download: it opens the file (on failure it logs `Cannot open <path>`
and the app keeps running with nothing playing), calls `AudioEngine::beginStream()`, then a 20 ms
timer, parented to the engine, appends 64 KiB per tick until the end of the file, calls
`finishData()` and deletes itself. It bypasses `Core::Player`: the playlist does not show the
file.

`DemoTracks()` returns nine fixed "Artist - Title" entries with the keys `demo`/`1`..`9` and
durations from 164 to 395 s. No port answers for the source `demo`: they have no cover and no web
page, the menus offer no like, dislike or "Open in the browser" for them, and they have no audio.

**Traps:**
- `StreamLocalFile` feeds the engine's *current* stream (the `appendData`/`finishData` overloads
  without a stream id). If something starts another stream meanwhile (a demo track played with
  `--demo`), the rest of the file goes into that stream.

## `main.cpp`

```cpp
constexpr int kSuccess = 0;
constexpr int kFailure = 1;
constexpr int kInternalError = 2;

int main(int argc, char* argv[]);  // Run() inside try/catch
```

`Run()`, in this order:

1. `ChoosePlatform()`, before `QApplication` exists, because it sets `QT_QPA_PLATFORM`.
2. Creates `QApplication` and sets the application name `QiYaa` (also the folder name Qt uses in
   per-application locations such as the cache), the desktop file name `qiyaa` (must match
   `packaging/linux/qiyaa.desktop`: taskbar icon, MPRIS), the window icon from the seven
   `:/icons/qiyaa-<size>.png`, the version `QIYAA_VERSION`, and
   `setQuitOnLastWindowClosed(false)`: closing the equalizer, the playlist or any other window
   never quits; only `Application::quit()` or the end of the session does.
3. Parses the options, fills `App::Application::Options`, constructs the `Application` and calls
   `start()`.
4. Applies `--scale`, `--demo` and `--text`, after `start()`, when the windows exist and are placed.
5. Takes the screenshot, or starts `--play-file`, or runs the event loop.

**Options.** The user reference is [docs/cli.md](../../docs/cli.md). What each one does here:

| Option | Effect |
|---|---|
| `--help`, `--version` | Qt's; `QCommandLineParser::process` prints and exits |
| `--screenshot <png>` | `offline`, `audio` off (unless `--play-file`), `readOnlySettings`, no media integration; see below |
| `--skin <wsz>` | `Options::skinOverride`; not stored in `skin` |
| `--play-file <path>` | `offline`, `audio` on; `StreamLocalFile(engine, path)` |
| `--offline` | `Options::offline` |
| `--text <text>` | `MainWindow::setStatusText(text)` |
| `--demo` | `Player::setSource(QueueSource{.title = "Demo", .head = DemoTracks()}, false)` |
| `--scale <factor>` | `windows()->setScale(value.toDouble(), WindowSet::ScaleScope::ThisRun)`: this run only |

Qt's own arguments (`-platform`, `-style`, …) are taken out by `QApplication` before the parser
runs. The mapping to `Options` is `offline = --screenshot || --offline || --play-file`,
`audio = !--screenshot || --play-file`, `readOnlySettings = --screenshot`,
`mediaIntegration = !--screenshot`.

**Platform choice** (Linux and FreeBSD only; elsewhere `ChoosePlatform()` does nothing). Wayland
does not let a client place its own windows, and the Winamp layout (separate top-level windows
snapped together) needs that. So:

1. `QT_QPA_PLATFORM` already set: left alone.
2. `QIYAA_NATIVE_WAYLAND=1` (exactly `1`): left alone, so Qt picks native Wayland on a Wayland
   session.
3. `WAYLAND_DISPLAY` set, or `XDG_SESSION_TYPE=wayland`: sets `QT_QPA_PLATFORM=xcb;wayland`, that
   is XWayland, with native Wayland as the fallback when xcb cannot start (for example, Qt's xcb
   plugin is not installed).

On native Wayland `SkinnedWindow::CanPositionWindows()` is false and window positions are not
saved.

**Screenshot.** Without `--play-file`: one `QApplication::processEvents()`, then
`windows()->snapshot()` saved to `<png>` (format from the extension, by `QImage::save`); `Run`
returns `kSuccess`, or `kFailure` when saving fails. The event loop never runs. There is no audio
device, no network, no media integration, and the settings are fresh defaults in a temporary
directory deleted at exit, so the image depends only on the options (`--skin`, `--scale`, `--demo`,
`--text`), the build and the platform. With `--play-file`, the file plays and the snapshot is taken
1500 ms later from the event loop, then `QApplication::exit(kSuccess or kFailure)`, so the
visualizer is moving in the image. CI runs `QiYaa --screenshot <png> --offline` on the offscreen
platform as a start-up check of every build and package.

**Exit codes:**

| Code | Name | When |
|---|---|---|
| 0 | `kSuccess` | normal quit; screenshot saved; `--help`, `--version` |
| 1 | `kFailure` | the screenshot could not be saved (unwritable path, unknown extension, no visible window). `QCommandLineParser::process` also exits with 1 on an unknown option or a missing value |
| 2 | `kInternalError` | a `std::exception` escaped `Run`: `main` prints `QiYaa: internal error: <what>` to stderr |

**Traps:**
- `Run` takes `int& argc` because `QApplication` keeps a reference to it; it must not become a copy.
- The `try`/`catch` in `main` covers what runs outside Qt's event loop: construction, `start()`,
  the options and the screenshot without `--play-file`. Exceptions are never thrown inside the
  event loop (see `CLAUDE.md`), and one thrown there would not arrive here intact.
- `--scale` goes through `QString::toDouble`: a non-number gives 0.0, which the windows clamp to
  1.0.
- `--text` is only the latest status text. Without `--offline`, the login messages replace it.
- The 1500 ms screenshot delay with `--play-file` is wall-clock time; how much of the visualizer
  it shows depends on the machine.

## Errors

`src/app` defines no exception type and throws nothing. It catches `Skins::Error` in two places:

- the constructor, for the skin from `--skin` or the `skin` setting: logs
  `Skin: <what>; using the built-in one` and keeps the base skin;
- `loadSkin()`: shows `Cannot load the skin: <what>` and returns false.

`Skins::Skin::BuiltinBase()`, in the member initialiser, is not caught: a broken bundled
`:/skins/base-2.91.wsz` is a bug. The `Skins::Error` leaves `Run`, and `main` turns it into exit
code 2. `Audio::Error` (equalizer preset files) never reaches this module; [src/ui](../ui/README.md)
catches it.

Failures that are data:

| Failure | How it shows |
|---|---|
| audio device | `AudioEngine::InitResult{ok, message}`: logs `Audio: <message>`, the app runs without sound |
| account connection | error string in the `Library::connectAccount` callback: marquee `Login failed: <error>` |
| login dialog cancelled | nothing happens |
| saving the token | `Yandex::SaveToken` returns false; ignored |
| `--play-file` cannot open | logs `Cannot open <path>` |
| screenshot not saved | `QImage::save` returns false: exit code 1 |
| settings file | `QSettings::status()` is never checked; an unwritable `settings.ini` loses changes silently |
| config folder | the result of `mkpath` is ignored |

## Not here

- Drawing, dragging, docking during a drag, shade, window resizing, per-window scale limits, the
  login dialog, the Yandex menu items, Milkdrop's own keys: [src/ui](../ui/README.md).
- Snapping geometry (`ConnectedGroup`, `SnapToOthers`, `PickScreen`, `ClampInside`):
  [src/ui](../ui/README.md), `snap.h`.
- The play queue, `shutDown()`, wave and play reports, the cover cache and its folder:
  [src/core](../core/README.md).
- Token lookup order, token file formats and permissions, OAuth, API requests, pending POSTs:
  [src/yandex](../yandex/README.md).
- `.wsz` loading and the bundled skins: [src/skins](../skins/README.md).
- The audio backend (`QIYAA_AUDIO_BACKEND`), equalizer DSP, `.eqf` presets:
  [src/audio](../audio/README.md).
- MPRIS, SMTC and macOS MediaPlayer: [src/integrations](../integrations/README.md).
- Milkdrop rendering and the preset list: [src/vis](../vis/README.md).
- The user-facing command-line reference: [docs/cli.md](../../docs/cli.md).
- The `.desktop` file, the Windows resource file and the installers: `packaging/`.
