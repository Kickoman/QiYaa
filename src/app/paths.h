#pragma once

#include <QString>
#include <QStringList>

class QTemporaryDir;

namespace App {

QString ConfigDirectory();

QStringList YaampDataDirectories();

QString TokenFile();
QString SpotifyCredentialsFile();
QStringList YaampTokenFiles();

QString SettingsFile(const QString& chosen, const QTemporaryDir* temporaryDirectory);
QString LocalCoversDirectory(const QTemporaryDir* temporaryDirectory);

}  // namespace App
