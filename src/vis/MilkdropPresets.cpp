#include "vis/MilkdropPresets.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>

namespace qiyaa {

namespace {
QList<MilkdropPresets::Preset> scan(const QString& dir, bool builtIn) {
    QList<MilkdropPresets::Preset> out;
    if (dir.isEmpty()) return out;
    const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.milk")}, QDir::Files | QDir::Readable);
    for (const QFileInfo& f : files) out.append({f.completeBaseName(), f.filePath(), builtIn});
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });
    return out;
}
}  // namespace

void MilkdropPresets::load(const QString& builtInDir, const QString& userDir) {
    m_presets = scan(builtInDir, true) + scan(userDir, false);
}

int MilkdropPresets::indexOf(const QString& name) const {
    for (int i = 0; i < size(); ++i)
        if (m_presets[i].name == name) return i;
    return -1;
}

QByteArray MilkdropPresets::data(int index) const {
    if (index < 0 || index >= size()) return {};
    QFile f(m_presets[index].path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();  // QByteArray keeps a terminating NUL after its data
}

int MilkdropPresets::next(int current) const {
    return isEmpty() ? -1 : (current + 1 + size()) % size();
}

int MilkdropPresets::previous(int current) const {
    return isEmpty() ? -1 : (current - 1 + size()) % size();
}

int MilkdropPresets::random(int current) const {
    if (isEmpty()) return -1;
    if (size() == 1) return 0;
    int i;
    do i = int(QRandomGenerator::global()->bounded(size()));
    while (i == current);
    return i;
}

}  // namespace qiyaa
