#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace Telemetry {

struct CrashReport {
    QString session;
    QString version;
    QString signal;
    QStringList frames;
    qint64 seconds = 0;
    QString exceptionType;
};

void InstallCrashHandler(const QString& path, const QString& session, const QString& version);

void UninstallCrashHandler();

std::optional<CrashReport> ReadCrashReport(const QString& path);

}  // namespace Telemetry
