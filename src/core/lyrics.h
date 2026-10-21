#pragma once

#include <QList>
#include <QString>
#include <QStringView>
#include <QtGlobal>

#include <functional>
#include <optional>

namespace Core {

inline constexpr qsizetype kMaxLyricsBytes = 256 * 1024;
inline constexpr qsizetype kMaxLyricsLines = 2000;
inline constexpr qsizetype kMaxLyricsLineCharacters = 500;

struct LyricsLine {
    qint64 ms = -1;
    QString text;

    bool operator==(const LyricsLine&) const = default;
};

enum class LyricsKind { Synced, Plain };

struct Lyrics {
    LyricsKind kind = LyricsKind::Plain;
    QList<LyricsLine> lines;

    bool operator==(const Lyrics&) const = default;
};

enum class LyricsFrom { Catalog, LrcFile, FileTags, ThirdParty };

struct LyricsAnswer {
    enum class Status { Found, Missing, Instrumental, Failed, NoAccount };

    Status status = Status::Missing;
    Lyrics lyrics;
    LyricsFrom from = LyricsFrom::Catalog;
    bool serverError = false;
};

using TLyricsDone = std::function<void(LyricsAnswer answer)>;

std::optional<Lyrics> ParseLyricsText(QStringView text);
std::optional<Lyrics> PlainLyrics(QStringView text);

}  // namespace Core
