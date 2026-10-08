#include "telemetry/crash_handler.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <cstdlib>
#include <stdexcept>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

// telemetry_test's crashing child: installs the crash handler, then fails as its last argument
// says (segv, terminate, abort). Arguments: <report path> <session> <version> <how>.

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() != 5) {
        return 2;
    }
#if defined(Q_OS_WIN)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    Telemetry::InstallCrashHandler(arguments[1], arguments[2], arguments[3]);
    const QString how = arguments[4];
    if (how == QLatin1String("segv")) {
        volatile std::uintptr_t nowhere = 0;
        *reinterpret_cast<volatile int*>(nowhere) = 1;
    } else if (how == QLatin1String("terminate")) {
        throw std::runtime_error("telemetry crash probe");
    } else if (how == QLatin1String("abort")) {
        std::abort();
    }
    return 3;
}
