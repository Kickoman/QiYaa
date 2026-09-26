#include "app/paths.h"

#include <QDir>
#include <QStandardPaths>

namespace App {

QString ConfigDirectory() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString directory = base + QStringLiteral("/QiYaa");
    QDir().mkpath(directory);
    return directory;
}

QStringList YaampDataDirectories() {
    QStringList bases;
#if defined(Q_OS_WIN)
    bases << qEnvironmentVariable("APPDATA");
#elif defined(Q_OS_MACOS)
    bases << QDir::homePath() + QStringLiteral("/Library/Application Support");
#else
    const QString xdgConfigHome = qEnvironmentVariable("XDG_CONFIG_HOME");
    bases
        << (xdgConfigHome.isEmpty() ? QDir::homePath() + QStringLiteral("/.config") : xdgConfigHome
           );
#endif
    QStringList out;
    for (const QString& base : bases) {
        if (base.isEmpty()) {
            continue;
        }
        out << base + QStringLiteral("/Yaamp") << base + QStringLiteral("/yaamp");
    }
    return out;
}

QString TokenFile() {
    return ConfigDirectory() + QStringLiteral("/token");
}

QStringList YaampTokenFiles() {
    QStringList out;
    for (const QString& directory : YaampDataDirectories()) {
        out << directory + QStringLiteral("/token.json");
    }
    return out;
}

}  // namespace App
