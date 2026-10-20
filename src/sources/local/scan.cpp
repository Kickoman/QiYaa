#include "sources/local/scan.h"

#include "sources/local/tags.h"
#include "sources/local/tracks.h"

#include <QBuffer>
#include <QCollator>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QSaveFile>
#include <QStringView>

#include <algorithm>
#include <optional>
#include <utility>

namespace LocalSource {

namespace {

const QStringList kCoverNames = {QStringLiteral("cover"), QStringLiteral("folder"), QStringLiteral("front")};
const QStringList kCoverSuffixes = {QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png")};

bool IsDigit(QChar character) {
    return character >= u'0' && character <= u'9';
}

QStringList RunsOf(const QString& text) {
    QStringList runs;
    for (qsizetype start = 0; start < text.size();) {
        const bool digits = IsDigit(text[start]);
        qsizetype end = start + 1;
        while (end < text.size() && IsDigit(text[end]) == digits) {
            ++end;
        }
        runs << text.mid(start, end - start);
        start = end;
    }
    return runs;
}

QStringView WithoutLeadingZeros(QStringView digits) {
    qsizetype first = 0;
    while (first + 1 < digits.size() && digits[first] == u'0') {
        ++first;
    }
    return digits.sliced(first);
}

int CompareRuns(const QString& left, const QString& right, const QCollator& collator) {
    const bool leftDigits = IsDigit(left.front());
    if (leftDigits != IsDigit(right.front())) {
        return leftDigits ? -1 : 1;
    }
    if (!leftDigits) {
        return collator.compare(left.toCaseFolded(), right.toCaseFolded());
    }
    const QStringView a = WithoutLeadingZeros(left);
    const QStringView b = WithoutLeadingZeros(right);
    if (a.size() != b.size()) {
        return a.size() < b.size() ? -1 : 1;
    }
    return a.compare(b);
}

int CompareNaturally(const QString& left, const QString& right, const QCollator& collator) {
    const QStringList a = RunsOf(left);
    const QStringList b = RunsOf(right);
    for (qsizetype i = 0; i < std::min(a.size(), b.size()); ++i) {
        if (const int order = CompareRuns(a[i], b[i], collator); order != 0) {
            return order;
        }
    }
    if (a.size() != b.size()) {
        return a.size() < b.size() ? -1 : 1;
    }
    return left.compare(right);
}

void SortNaturally(QStringList& paths) {
    const QCollator collator;
    std::sort(paths.begin(), paths.end(), [&](const QString& left, const QString& right) {
        const QStringList a = QDir::fromNativeSeparators(left).split(u'/');
        const QStringList b = QDir::fromNativeSeparators(right).split(u'/');
        for (qsizetype i = 0; i < std::min(a.size(), b.size()); ++i) {
            if (const int order = CompareNaturally(a[i], b[i], collator); order != 0) {
                return order < 0;
            }
        }
        return a.size() < b.size();
    });
}

QString FolderImage(const QDir& folder) {
    const QFileInfoList files = folder.entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
    for (const QString& name : kCoverNames) {
        for (const QFileInfo& file : files) {
            if (file.completeBaseName().compare(name, Qt::CaseInsensitive) == 0
                && kCoverSuffixes.contains(file.suffix(), Qt::CaseInsensitive)) {
                return file.absoluteFilePath();
            }
        }
    }
    return {};
}

QByteArray ReadBounded(const QString& path) {
    QFile file(path);
    if (file.size() > kMaxPictureBytes || !file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.read(kMaxPictureBytes);
}

class Covers {
public:
    explicit Covers(QString coverDirectory)
        : directory(std::move(coverDirectory)) { }

    QString of(const QString& path, const QByteArray& picture) {
        if (!picture.isEmpty()) {
            return written(picture);
        }
        const QString folder = QFileInfo(path).absolutePath();
        if (const auto known = byFolder.constFind(folder); known != byFolder.cend()) {
            return *known;
        }
        const QString image = FolderImage(QDir(folder));
        const QString cover = image.isEmpty() ? QString() : written(ReadBounded(image));
        byFolder.insert(folder, cover);
        return cover;
    }

private:
    QString written(const QByteArray& picture) {
        if (picture.isEmpty() || directory.isEmpty()) {
            return {};
        }
        const QString name = QString::fromLatin1(QCryptographicHash::hash(picture, QCryptographicHash::Sha1).toHex());
        const QString path = directory + u'/' + name + QStringLiteral(".jpg");
        if (QFileInfo::exists(path)) {
            return path;
        }
        QBuffer buffer;
        buffer.setData(picture);
        QImageReader reader(&buffer);
        const QSize size = reader.size();
        if (size.isValid() && std::max(size.width(), size.height()) > kCoverPixels) {
            reader.setScaledSize(size.scaled(kCoverPixels, kCoverPixels, Qt::KeepAspectRatio));
        }
        const QImage image = reader.read();
        if (image.isNull() || !QDir().mkpath(directory)) {
            return {};
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "JPG", 90) || !file.commit()) {
            return {};
        }
        return path;
    }

    const QString directory;
    QHash<QString, QString> byFolder;
};

QFileInfoList EntriesOf(const QString& folder, const QCollator& collator) {
    QFileInfoList entries;
    const QFileInfoList all =
        QDir(folder).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
    for (const QFileInfo& entry : all) {
        if (entry.fileName().startsWith(u'.')) {
            continue;
        }
        const bool folderLink = entry.isDir() && (entry.isSymLink() || entry.isJunction());
        if (!folderLink && (entry.isDir() || HasAudioExtension(entry.fileName()))) {
            entries << entry;
        }
    }
    std::sort(entries.begin(), entries.end(), [&](const QFileInfo& left, const QFileInfo& right) {
        return CompareNaturally(left.fileName(), right.fileName(), collator) < 0;
    });
    return entries;
}

ScanResult TracksOf(const QStringList& paths, const QString& coverDirectory, const std::atomic<bool>& cancelled) {
    ScanResult result;
    Covers covers(coverDirectory);
    for (const QString& path : paths) {
        if (cancelled) {
            return {};
        }
        const std::optional<FileTags> tags = ReadTags(path);
        if (tags && !tags->playable) {
            continue;
        }
        result.tracks << TrackOf(path, tags, covers.of(path, tags ? tags->picture : QByteArray()));
    }
    return result;
}

}  // namespace

ScanResult ScanFiles(const QStringList& paths, const QString& coverDirectory, const std::atomic<bool>& cancelled) {
    QStringList files;
    for (const QString& path : paths) {
        if (QFileInfo(path).isFile() && HasAudioExtension(path)) {
            files << QFileInfo(path).absoluteFilePath();
        }
    }
    SortNaturally(files);
    const bool cut = files.size() > kMaxFiles;
    ScanResult result = TracksOf(files.mid(0, kMaxFiles), coverDirectory, cancelled);
    result.cut = cut;
    return result;
}

ScanResult ScanFolder(const QString& folder, const QString& coverDirectory, const std::atomic<bool>& cancelled) {
    struct Level {
        QFileInfoList entries;
        qsizetype next = 0;
    };
    const QCollator collator;
    QStringList files;
    bool cut = false;
    QList<Level> levels{Level{EntriesOf(QFileInfo(folder).absoluteFilePath(), collator)}};
    int visited = 1;
    while (!levels.isEmpty() && !cut && !cancelled) {
        Level& level = levels.last();
        if (level.next == level.entries.size()) {
            levels.removeLast();
            continue;
        }
        const QFileInfo entry = level.entries[level.next++];
        if (!entry.isDir()) {
            cut = files.size() == kMaxFiles;
            if (!cut) {
                files << entry.absoluteFilePath();
            }
        } else if (visited == kMaxFolders) {
            cut = true;
        } else {
            ++visited;
            levels << Level{EntriesOf(entry.absoluteFilePath(), collator)};
        }
    }
    ScanResult result = TracksOf(files, coverDirectory, cancelled);
    result.cut = cut;
    return result;
}

}  // namespace LocalSource
