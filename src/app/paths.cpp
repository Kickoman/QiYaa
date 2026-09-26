#include "app/paths.h"

#include <QDir>
#include <QStandardPaths>

namespace App {

QString ConfigDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString dir = base + QStringLiteral("/QiYaa");
    QDir().mkpath(dir);
    return dir;
}

QStringList YaampDataDirs() {
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
        if (b.isEmpty()) {
            continue;
        }
        out << b + QStringLiteral("/Yaamp") << b + QStringLiteral("/yaamp");
    }
    return out;
}

QString TokenFile() {
    return ConfigDir() + QStringLiteral("/token");
}

QStringList YaampTokenFiles() {
    QStringList out;
    for (const QString& directory : YaampDataDirs()) {
        out << directory + QStringLiteral("/token.json");
    }
    return out;
}

}  // namespace App
