#pragma once

#include <QString>
#include <QStringList>

namespace App {

QString ConfigDir();

QStringList YaampDataDirs();

QString TokenFile();
QStringList YaampTokenFiles();

}  // namespace App
