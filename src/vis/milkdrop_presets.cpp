#include "vis/milkdrop_presets.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>

#include <algorithm>

namespace Vis {

namespace {

// The largest preset of the Cream of the Crop collection is 60 KB.
constexpr qint64 kMaxPresetBytes = 1024 * 1024;

QList<MilkdropPresets::Preset> ScanPresetDirectory(const QString& dir, bool builtIn) {
    QList<MilkdropPresets::Preset> out;
    if (dir.isEmpty()) {
        return out;
    }
    const QFileInfoList files =
        QDir(dir).entryInfoList({QStringLiteral("*.milk")}, QDir::Files | QDir::Readable);
    for (const QFileInfo& file : files) {
        out.append({file.completeBaseName(), file.filePath(), builtIn});
    }
    std::sort(out.begin(), out.end(), [](const auto& first, const auto& second) {
        if (const int byName = QString::compare(first.name, second.name, Qt::CaseInsensitive)) {
            return byName < 0;
        }
        return first.path < second.path;
    });
    return out;
}
}  // namespace

void MilkdropPresets::load(const QString& builtInDir, const QString& userDir) {
    presetList = ScanPresetDirectory(builtInDir, true) + ScanPresetDirectory(userDir, false);
}

int MilkdropPresets::indexOf(const QString& name) const {
    for (int i = 0; i < size(); ++i) {
        if (presetList[i].name == name) {
            return i;
        }
    }
    return -1;
}

QByteArray MilkdropPresets::data(int index) const {
    if (index < 0 || index >= size()) {
        return {};
    }
    QFile file(presetList[index].path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxPresetBytes) {
        return {};
    }
    return file.readAll();  // QByteArray keeps a terminating NUL after its data
}

int MilkdropPresets::next(int current) const {
    return isEmpty() ? -1 : (current + 1 + size()) % size();
}

int MilkdropPresets::previous(int current) const {
    return isEmpty() ? -1 : (current - 1 + size()) % size();
}

int MilkdropPresets::random(int current) const {
    if (isEmpty()) {
        return -1;
    }
    if (size() == 1) {
        return 0;
    }
    int i;
    do {
        i = int(QRandomGenerator::global()->bounded(size()));
    } while (i == current);
    return i;
}

}  // namespace Vis
