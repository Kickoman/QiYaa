#pragma once

#include <QString>
#include <QStringList>

namespace App {

// Our own config directory (created on demand), e.g. ~/.config/QiYaa.
QString configDir();

// Candidate locations of the old Electron Yaamp data folder (userData).
QStringList yaampDataDirs();

}  // namespace App
