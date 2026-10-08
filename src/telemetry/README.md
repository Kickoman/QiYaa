# `src/telemetry` — usage statistics and crash reports

Built only with `QIYAA_WITH_TELEMETRY=ON` (the releases), which defines `QIYAA_HAVE_TELEMETRY`.
The module records the events of [spec/telemetry](../../spec/telemetry/README.md), keeps them on
disk until the jam server takes them, writes a crash report when the process crashes and reports
earlier runs that crashed or did not close. It knows nothing of the player: `App::Application`
fills the start event, connects the error signals and owns the `Reporter`; the menu switch is
there too ([src/app](../app/README.md)). The jam server's side is `POST /api/telemetry` in
Kickoman/QiYaa-jam.

| File | Contains |
|---|---|
| `reporter.h/.cpp` | `Telemetry::Reporter`: the queue, the session's files, sending, the session's counters |
| `crash_handler.h/.cpp` | `InstallCrashHandler`, `UninstallCrashHandler`, `ReadCrashReport`, `CrashReport` |
| `machine_id.h/.cpp` | `MachineId`, `MachineIdFor`: the app's hash of the OS's machine id |
| `system_info.h/.cpp` | `SystemFields`: the system part of the start event |

Dependencies: Qt Core and Network (public), Qt Gui for the screens; `dbghelp` on Windows, the
platform's `dl` elsewhere. Nothing of QiYaa's own.

## `reporter.h`

```cpp
class Reporter : public QObject {
    struct Config { QUrl endpoint; QString directory; QString version; QString machine;
                    QNetworkAccessManager* network; int sendIntervalMs = 5 min; };
    void begin(const QJsonObject& startFields);  // earlier runs, the crash handler, `start`
    void recordError(area, kind, httpStatus = 0);  // at most kMaxErrorsPerSession, then counted
    void recordFeature(name, value);
    void countTrack(); void countJam(); void setPlaying(bool); void setMilkdropShown(bool);
    void finish();       // `exit`; from here the run counts as closed
    void send();         // one batch, if allowed now
    void forget();       // TEL-02: deletes this run's files, sends nothing more
    static void ForgetAll(const QString& directory);  // the files of runs not running now
    Q_SIGNAL void sendingDone();
};
```

A run's files in `directory`, named by its session id:

| File | Holds | Removed |
|---|---|---|
| `<session>.lock` | `QLockFile`: the run is alive (its PID) | at the end of the run |
| `<session>.json` | `{"version", "seconds"}`, rewritten every minute | at `finish()`: a run with it left over did not close |
| `<session>.crash` | the crash handler's report, empty while all is well | at the end of the run |
| `<session>.queue` | the events not sent yet, one JSON per line | when it is empty |

`begin()` looks at the other sessions in the folder. One whose lock is held is a running
instance and is left alone. For a dead one, its queue joins this run's. Then it is reported:
`crash` if its `.crash` reads as a report, otherwise `unclean_exit` from its `.json`. Its files
go. A batch takes events from the front of the queue: up to 50, and up to 60 KiB of JSON (the
server takes 64). The answer decides what happens next:
- 2xx, 400 or 413 drops the events;
- 429 waits `Retry-After` (60 s without it);
- anything else, or no answer, keeps them and waits 1 min, doubling up to 1 h.

**Traps:**
- the crash handler is per process: a second `Reporter` in one process (tests) does not install
  its own, and the first one's end uninstalls it;
- `finish()` removes the `.json` before the last batch goes, so being killed while sending is
  not an unclean exit; being killed before `finish()` is.

## `crash_handler.h`

```cpp
void InstallCrashHandler(const QString& path, const QString& session, const QString& version);
void UninstallCrashHandler();  // restores the handlers it replaced
std::optional<CrashReport> ReadCrashReport(const QString& path);  // nullopt: none or not one
```

The report is text, `key=value` per line: `session`, `version`, `signal`, `seconds`,
`exception` (the type of an uncaught exception) and one `frame=<module>+0x<offset>` per frame,
up to 32. The handlers write it with nothing but `write()` / `WriteFile` into a file opened at
install, from fixed buffers, and number formatting of their own: no allocation after a crash.

| Platform | Catches | Frames |
|---|---|---|
| Linux, macOS | `sigaction` on SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, on an alternate stack | `backtrace()` (loaded at install), starting at the interrupted instruction from `ucontext`; `dladdr` gives the module and its base |
| Windows | `SetUnhandledExceptionFilter`, `signal(SIGABRT)` | `StackWalk64` over the exception's context (`SymInitialize` at install, deferred loads); `GetModuleHandleEx` gives the module |
| all | `std::set_terminate`: an uncaught C++ exception, as `TERMINATE` with its type's name | the terminate handler's own stack |

After writing, a signal handler puts back the default action and raises the signal again. The
Windows filter passes the exception on. So the OS still sees a crash: a core dump, WER,
ReportCrash. `ReadCrashReport` checks every field against the patterns of the schema, renames
an unknown module `unknown`, and replaces characters a module name may not have with `_`.

## `machine_id.h`, `system_info.h`

`MachineId(systemId)` is the first 16 bytes, in hex, of HMAC-SHA256 with the key `QiYaa
telemetry` over `systemId`. `MachineIdFor(settings)` takes `QSysInfo::machineUniqueId()`, or,
when the OS has none, a random UUID kept as `telemetry/machine` in the settings.
`SystemFields()` gives `os`, `osName`, `osVersion`, `kernel`, `arch`, `qt`, `package` (from
`QIYAA_PACKAGE`), `locale`, `screens` and `dpr`, each cut to its schema's pattern.
