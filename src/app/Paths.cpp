#include "app/Paths.h"

#include <QDir>
#include <QStandardPaths>

namespace qiyaa::paths {

QString configDir() {
    // GenericConfigLocation: ~/.config, %LOCALAPPDATA%, ~/Library/Preferences
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString dir = base + QStringLiteral("/QiYaa");
    QDir().mkpath(dir);
    return dir;
}

QStringList yaampDataDirs() {
    // Electron's app.getPath('userData') = <appData>/<productName>.
    // appData: ~/.config (Linux), %APPDATA% (Windows), ~/Library/Application Support (macOS).
    QStringList bases;
#if defined(Q_OS_WIN)
    bases << qEnvironmentVariable("APPDATA");
#elif defined(Q_OS_MACOS)
    bases << QDir::homePath() + QStringLiteral("/Library/Application Support");
#else
    const QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
    bases << (xdg.isEmpty() ? QDir::homePath() + QStringLiteral("/.config") : xdg);
#endif
    QStringList out;
    for (const QString& b : bases) {
        if (b.isEmpty()) continue;
        out << b + QStringLiteral("/Yaamp") << b + QStringLiteral("/yaamp");
    }
    return out;
}

}  // namespace qiyaa::paths
