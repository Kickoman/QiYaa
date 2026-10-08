#include "telemetry/crash_handler.h"

#include <QFile>
#include <QRegularExpression>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <typeinfo>

#if defined(Q_OS_WIN)
#include <windows.h>
// dbghelp.h needs windows.h first.
#include <dbghelp.h>

#include <csignal>
#else
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#endif

namespace Telemetry {

namespace {

constexpr int kMaxFrames = 32;

std::atomic<bool> installed{false};
std::atomic<bool> reported{false};
char header[160];
std::size_t headerLength = 0;
std::terminate_handler previousTerminate = nullptr;

#if defined(Q_OS_WIN)
HANDLE reportFile = INVALID_HANDLE_VALUE;
ULONGLONG startedAt = 0;
LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;

void WriteAll(const char* text, std::size_t length) {
    DWORD written = 0;
    WriteFile(reportFile, text, static_cast<DWORD>(length), &written, nullptr);
}

std::int64_t Uptime() {
    return static_cast<std::int64_t>((GetTickCount64() - startedAt) / 1000);
}
#else
int reportFd = -1;
timespec startedAt{};
constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
struct sigaction previousActions[std::size(kSignals)];
alignas(16) char alternateStack[64 * 1024];

void WriteAll(const char* text, std::size_t length) {
    while (length > 0) {
        const ssize_t written = write(reportFd, text, length);
        if (written <= 0) {
            return;
        }
        text += written;
        length -= static_cast<std::size_t>(written);
    }
}

std::int64_t Uptime() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::int64_t>(now.tv_sec - startedAt.tv_sec);
}
#endif

void WriteText(const char* text) {
    WriteAll(text, std::strlen(text));
}

void WriteHex(std::uintptr_t value) {
    char digits[2 + 2 * sizeof(value)];
    std::size_t length = 0;
    do {
        digits[length++] = "0123456789abcdef"[value & 0xf];
        value >>= 4;
    } while (value != 0);
    char text[sizeof(digits) + 2] = {'0', 'x'};
    for (std::size_t i = 0; i < length; ++i) {
        text[2 + i] = digits[length - 1 - i];
    }
    WriteAll(text, length + 2);
}

void WriteDecimal(std::int64_t value) {
    char digits[24];
    std::size_t length = 0;
    std::uint64_t magnitude = value < 0 ? 0 : static_cast<std::uint64_t>(value);
    do {
        digits[length++] = static_cast<char>('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude != 0);
    char text[sizeof(digits)];
    for (std::size_t i = 0; i < length; ++i) {
        text[i] = digits[length - 1 - i];
    }
    WriteAll(text, length);
}

void WriteFrame(const char* module, std::uintptr_t offset) {
    WriteText("frame=");
    WriteText(module ? module : "?");
    WriteText("+");
    WriteHex(offset);
    WriteText("\n");
}

void BeginReport(const char* signal, const char* exceptionType) {
    WriteAll(header, headerLength);
    WriteText("signal=");
    WriteText(signal);
    WriteText("\nseconds=");
    WriteDecimal(Uptime());
    WriteText("\n");
    if (exceptionType) {
        WriteText("exception=");
        WriteText(exceptionType);
        WriteText("\n");
    }
}

#if defined(Q_OS_WIN)

const char* BaseName(const wchar_t* path, char* buffer, std::size_t size) {
    const wchar_t* name = path;
    for (const wchar_t* character = path; *character; ++character) {
        if (*character == L'\\' || *character == L'/') {
            name = character + 1;
        }
    }
    std::size_t length = 0;
    for (; name[length] && length + 1 < size; ++length) {
        const wchar_t character = name[length];
        buffer[length] = character < 0x80 ? static_cast<char>(character) : '_';
    }
    buffer[length] = 0;
    return buffer;
}

void WriteAddress(DWORD64 address) {
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH];
    char name[MAX_PATH];
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address), &module
        )
        && GetModuleFileNameW(module, path, MAX_PATH) > 0) {
        WriteFrame(
            BaseName(path, name, sizeof(name)),
            static_cast<std::uintptr_t>(address - reinterpret_cast<DWORD64>(module))
        );
    } else {
        WriteFrame(nullptr, static_cast<std::uintptr_t>(address));
    }
}

const char* ExceptionName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
        case 0xC0000374: return "STATUS_HEAP_CORRUPTION";
        case 0xC0000409: return "STATUS_STACK_BUFFER_OVERRUN";
        case 0xE06D7363: return "CPP_EXCEPTION";
        default: return "EXCEPTION_OTHER";
    }
}

void WriteStack(CONTEXT context) {
    STACKFRAME64 frame{};
    DWORD machine = 0;
#if defined(_M_X64)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_ARM64)
    machine = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset = context.Pc;
    frame.AddrFrame.Offset = context.Fp;
    frame.AddrStack.Offset = context.Sp;
#endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;
    const HANDLE process = GetCurrentProcess();
    const HANDLE thread = GetCurrentThread();
    for (int count = 0; count < kMaxFrames && machine != 0; ++count) {
        if (!StackWalk64(
                machine, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64,
                SymGetModuleBase64, nullptr
            )
            || frame.AddrPC.Offset == 0) {
            break;
        }
        WriteAddress(frame.AddrPC.Offset);
    }
}

void WriteCurrentStack(int skip) {
    void* frames[kMaxFrames + 4];
    const USHORT count = RtlCaptureStackBackTrace(0, kMaxFrames + 4, frames, nullptr);
    for (int i = skip; i < count && i - skip < kMaxFrames; ++i) {
        WriteAddress(reinterpret_cast<DWORD64>(frames[i]));
    }
}

LONG WINAPI OnException(EXCEPTION_POINTERS* exception) {
    if (!reported.exchange(true)) {
        BeginReport(ExceptionName(exception->ExceptionRecord->ExceptionCode), nullptr);
        WriteStack(*exception->ContextRecord);
        FlushFileBuffers(reportFile);
    }
    return previousFilter ? previousFilter(exception) : EXCEPTION_CONTINUE_SEARCH;
}

void OnAbort(int) {
    if (!reported.exchange(true)) {
        BeginReport("SIGABRT", nullptr);
        WriteCurrentStack(1);
        FlushFileBuffers(reportFile);
    }
}

#else

const char* SignalName(int signal) {
    switch (signal) {
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS: return "SIGBUS";
        case SIGILL: return "SIGILL";
        case SIGFPE: return "SIGFPE";
        case SIGABRT: return "SIGABRT";
        default: return "SIGNAL";
    }
}

std::uintptr_t ProgramCounter(void* context) {
    [[maybe_unused]] auto* user = static_cast<ucontext_t*>(context);
#if defined(__linux__) && defined(__x86_64__)
    return static_cast<std::uintptr_t>(user->uc_mcontext.gregs[REG_RIP]);
#elif defined(__linux__) && defined(__aarch64__)
    return static_cast<std::uintptr_t>(user->uc_mcontext.pc);
#elif defined(__APPLE__) && defined(__aarch64__)
    return static_cast<std::uintptr_t>(user->uc_mcontext->__ss.__pc);
#elif defined(__APPLE__) && defined(__x86_64__)
    return static_cast<std::uintptr_t>(user->uc_mcontext->__ss.__rip);
#else
    return 0;
#endif
}

void WriteAddress(void* address) {
    Dl_info info{};
    if (dladdr(address, &info) != 0 && info.dli_fname && info.dli_fbase) {
        const char* name = std::strrchr(info.dli_fname, '/');
        WriteFrame(
            name ? name + 1 : info.dli_fname,
            reinterpret_cast<std::uintptr_t>(address)
                - reinterpret_cast<std::uintptr_t>(info.dli_fbase)
        );
    } else {
        WriteFrame(nullptr, reinterpret_cast<std::uintptr_t>(address));
    }
}

// From the interrupted instruction when it is known: the frames before it are the handler's own.
void WriteStack(std::uintptr_t programCounter, int skip) {
    void* frames[kMaxFrames + 8];
    const int count = backtrace(frames, kMaxFrames + 8);
    int first = skip;
    if (programCounter != 0) {
        WriteAddress(reinterpret_cast<void*>(programCounter));
        for (int i = 0; i < count; ++i) {
            if (reinterpret_cast<std::uintptr_t>(frames[i]) == programCounter) {
                first = i + 1;
                break;
            }
        }
    }
    for (int i = first, written = programCounter != 0 ? 1 : 0; i < count && written < kMaxFrames;
         ++i, ++written) {
        WriteAddress(frames[i]);
    }
}

void OnSignal(int signal, siginfo_t*, void* context) {
    if (!reported.exchange(true)) {
        BeginReport(SignalName(signal), nullptr);
        WriteStack(signal == SIGABRT ? 0 : ProgramCounter(context), 1);
    }
    struct sigaction standard { };
    standard.sa_handler = SIG_DFL;
    sigemptyset(&standard.sa_mask);
    sigaction(signal, &standard, nullptr);
    raise(signal);
}

#endif

[[noreturn]] void OnTerminate() {
    const char* type = "unknown";
    if (const std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& error) {
            type = typeid(error).name();
        } catch (...) {
        }
    }
    if (!reported.exchange(true)) {
        BeginReport("TERMINATE", type);
#if defined(Q_OS_WIN)
        WriteCurrentStack(1);
        FlushFileBuffers(reportFile);
#else
        WriteStack(0, 1);
#endif
    }
    std::abort();
}

}  // namespace

void InstallCrashHandler(const QString& path, const QString& session, const QString& version) {
    if (installed.exchange(true)) {
        return;
    }
    reported = false;
    const QByteArray text =
        QStringLiteral("session=%1\nversion=%2\n").arg(session, version).toLatin1();
    headerLength = std::min<std::size_t>(static_cast<std::size_t>(text.size()), sizeof(header));
    std::memcpy(header, text.constData(), headerLength);
#if defined(Q_OS_WIN)
    startedAt = GetTickCount64();
    reportFile = CreateFileW(
        reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    SymSetOptions(SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    previousFilter = SetUnhandledExceptionFilter(OnException);
    std::signal(SIGABRT, OnAbort);
#else
    clock_gettime(CLOCK_MONOTONIC, &startedAt);
    reportFd =
        open(QFile::encodeName(path).constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    void* warmUp[1];
    backtrace(warmUp, 1);  // loads the unwinder now, not inside a signal handler
    stack_t stack{};
    stack.ss_sp = alternateStack;
    stack.ss_size = sizeof(alternateStack);
    sigaltstack(&stack, nullptr);
    for (std::size_t i = 0; i < std::size(kSignals); ++i) {
        struct sigaction action { };
        action.sa_sigaction = OnSignal;
        action.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&action.sa_mask);
        sigaction(kSignals[i], &action, &previousActions[i]);
    }
#endif
    previousTerminate = std::set_terminate(OnTerminate);
}

void UninstallCrashHandler() {
    if (!installed.exchange(false)) {
        return;
    }
    std::set_terminate(previousTerminate);
#if defined(Q_OS_WIN)
    std::signal(SIGABRT, SIG_DFL);
    SetUnhandledExceptionFilter(previousFilter);
    SymCleanup(GetCurrentProcess());
    if (reportFile != INVALID_HANDLE_VALUE) {
        CloseHandle(reportFile);
        reportFile = INVALID_HANDLE_VALUE;
    }
#else
    for (std::size_t i = 0; i < std::size(kSignals); ++i) {
        sigaction(kSignals[i], &previousActions[i], nullptr);
    }
    if (reportFd >= 0) {
        close(reportFd);
        reportFd = -1;
    }
#endif
}

std::optional<CrashReport> ReadCrashReport(const QString& path) {
    static const QRegularExpression session(
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")
    );
    static const QRegularExpression version(QStringLiteral("^[0-9A-Za-z.+_-]{1,32}$"));
    static const QRegularExpression signal(QStringLiteral("^[A-Z][A-Z0-9_]{0,39}$"));
    static const QRegularExpression frame(QStringLiteral("^(.*)\\+0x([0-9a-f]{1,16})$"));
    static const QRegularExpression notModule(QStringLiteral("[^A-Za-z0-9._+-]"));
    static const QRegularExpression typeName(QStringLiteral("^[A-Za-z0-9_:<>, *&]{1,96}$"));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024) {
        return std::nullopt;
    }
    CrashReport report;
    for (const QByteArray& line : file.readAll().split('\n')) {
        const qsizetype equals = line.indexOf('=');
        if (equals < 0) {
            continue;
        }
        const QString key = QString::fromLatin1(line.left(equals));
        const QString value = QString::fromLatin1(line.mid(equals + 1));
        if (key == QLatin1String("session")) {
            report.session = value;
        } else if (key == QLatin1String("version")) {
            report.version = value;
        } else if (key == QLatin1String("signal")) {
            report.signal = value;
        } else if (key == QLatin1String("seconds")) {
            report.seconds = std::max<qint64>(0, value.toLongLong());
        } else if (key == QLatin1String("exception") && typeName.match(value).hasMatch()) {
            report.exceptionType = value;
        } else if (key == QLatin1String("frame") && report.frames.size() < kMaxFrames) {
            const QRegularExpressionMatch parts = frame.match(value);
            if (parts.hasMatch()) {
                QString module = parts.captured(1);
                module = module == QLatin1String("?")
                    ? QStringLiteral("unknown")
                    : module.replace(notModule, QStringLiteral("_")).left(64);
                report.frames << (module.isEmpty() ? QStringLiteral("unknown") : module)
                        + QStringLiteral("+0x") + parts.captured(2);
            }
        }
    }
    if (!session.match(report.session).hasMatch() || !version.match(report.version).hasMatch()
        || !signal.match(report.signal).hasMatch()) {
        return std::nullopt;
    }
    return report;
}

}  // namespace Telemetry
