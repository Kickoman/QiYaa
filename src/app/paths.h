#pragma once

#include <QString>
#include <QStringList>

namespace App {

QString ConfigDirectory();

QStringList YaampDataDirectories();

QString TokenFile();
QStringList YaampTokenFiles();

}  // namespace App
