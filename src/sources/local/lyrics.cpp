#include "sources/local/lyrics.h"

#include "sources/local/tags.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QStringConverter>
#include <QStringDecoder>
#include <QStringList>

namespace LocalSource {

namespace {

bool HasKind(const std::optional<Core::Lyrics>& lyrics, Core::LyricsKind kind) {
    return lyrics && lyrics->kind == kind;
}

Core::LyricsAnswer Found(const Core::Lyrics& lyrics, Core::LyricsFrom from) {
    return Core::LyricsAnswer{.status = Core::LyricsAnswer::Status::Found, .lyrics = lyrics, .from = from};
}

}  // namespace

QString LrcPathOf(const QString& audioPath) {
    const QFileInfo audio(audioPath);
    const QDir folder = audio.dir();
    const QString base = audio.completeBaseName();
    for (const QString& name : folder.entryList({QStringLiteral("*.lrc")}, QDir::Files, QDir::Name)) {
        if (QFileInfo(name).completeBaseName() == base) {
            return folder.filePath(name);
        }
    }
    return {};
}

std::optional<QString> ReadLrc(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > Core::kMaxLyricsBytes) {
        return std::nullopt;
    }
    const QByteArray bytes = file.read(Core::kMaxLyricsBytes + 1);
    if (bytes.size() > Core::kMaxLyricsBytes) {
        return std::nullopt;
    }
    if (const std::optional<QStringConverter::Encoding> marked = QStringConverter::encodingForData(bytes)) {
        QStringDecoder decoder(*marked);
        return QString(decoder(bytes));
    }
    QStringDecoder utf8(QStringConverter::Utf8);
    const QString text = utf8(bytes);
    if (!utf8.hasError()) {
        return text;
    }
    QStringDecoder system(QStringConverter::System);
    return QString(system(bytes));
}

Core::LyricsAnswer ReadLyrics(const QString& audioPath) {
    std::optional<Core::Lyrics> lrc;
    if (const QString lrcPath = LrcPathOf(audioPath); !lrcPath.isEmpty()) {
        if (const std::optional<QString> text = ReadLrc(lrcPath)) {
            lrc = Core::ParseLyricsText(*text);
        }
    }
    if (HasKind(lrc, Core::LyricsKind::Synced)) {
        return Found(*lrc, Core::LyricsFrom::LrcFile);
    }
    const std::optional<Core::Lyrics> tags = Core::ParseLyricsText(ReadTagLyrics(audioPath));
    if (tags) {
        return Found(*tags, Core::LyricsFrom::FileTags);
    }
    if (lrc) {
        return Found(*lrc, Core::LyricsFrom::LrcFile);
    }
    return {};
}

}  // namespace LocalSource
