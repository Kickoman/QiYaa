#include "core/lyrics.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace Core {

namespace {

constexpr qsizetype kMaxMinuteDigits = 7;
constexpr qint64 kMaxOffsetMs = 24LL * 60 * 60 * 1000;

const QStringList kIdTags = {
    QStringLiteral("ar"),
    QStringLiteral("al"),
    QStringLiteral("ti"),
    QStringLiteral("au"),
    QStringLiteral("by"),
    QStringLiteral("length"),
    QStringLiteral("offset"),
    QStringLiteral("re"),
    QStringLiteral("tool"),
    QStringLiteral("ve"),
    QStringLiteral("la"),
    QStringLiteral("id"),
    QStringLiteral("#"),
};

struct TimeTag {
    qint64 ms = 0;
    qsizetype length = 0;
};

struct Timed {
    qint64 ms = 0;
    QString text;
};

bool IsDigit(QChar c) {
    return c >= u'0' && c <= u'9';
}

std::optional<TimeTag> TimeTagAt(QStringView line, qsizetype from) {
    qsizetype at = from;
    if (at >= line.size() || line[at] != u'[') {
        return std::nullopt;
    }
    ++at;
    qint64 minutes = 0;
    const qsizetype minutesFrom = at;
    while (at < line.size() && IsDigit(line[at]) && at - minutesFrom < kMaxMinuteDigits) {
        minutes = minutes * 10 + line[at].unicode() - u'0';
        ++at;
    }
    if (at == minutesFrom || at + 3 > line.size() || line[at] != u':' || !IsDigit(line[at + 1])
        || !IsDigit(line[at + 2])) {
        return std::nullopt;
    }
    const int seconds = (line[at + 1].unicode() - u'0') * 10 + line[at + 2].unicode() - u'0';
    at += 3;
    if (seconds > 59) {
        return std::nullopt;
    }
    qint64 fractionMs = 0;
    if (at < line.size() && (line[at] == u'.' || line[at] == u':')) {
        ++at;
        const qsizetype fractionFrom = at;
        int scale = 100;
        while (at < line.size() && IsDigit(line[at])) {
            if (scale > 0) {
                fractionMs += (line[at].unicode() - u'0') * scale;
                scale /= 10;
            }
            ++at;
        }
        if (at == fractionFrom) {
            return std::nullopt;
        }
    }
    if (at >= line.size() || line[at] != u']') {
        return std::nullopt;
    }
    return TimeTag{(minutes * 60 + seconds) * 1000 + fractionMs, at + 1 - from};
}

std::optional<std::pair<QString, QString>> IdTagOf(const QString& line) {
    static const QRegularExpression idTag(QStringLiteral("^\\[([A-Za-z#]+):(.*)\\]$"));
    const QRegularExpressionMatch match = idTag.match(line);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    const QString key = match.captured(1).toLower();
    if (!kIdTags.contains(key)) {
        return std::nullopt;
    }
    return std::pair{key, match.captured(2).trimmed()};
}

QString Cut(const QString& text) {
    if (text.size() <= kMaxLyricsLineCharacters) {
        return text;
    }
    const bool splitsPair = text[kMaxLyricsLineCharacters - 1].isHighSurrogate();
    return text.left(splitsPair ? kMaxLyricsLineCharacters - 1 : kMaxLyricsLineCharacters);
}

std::optional<QStringList> LinesOf(QStringView text) {
    if (text.toUtf8().size() > kMaxLyricsBytes) {
        return std::nullopt;
    }
    if (text.startsWith(QChar(0xFEFF))) {
        text = text.mid(1);
    }
    static const QRegularExpression lineEnd(QStringLiteral("\r\n|\r|\n"));
    return text.toString().split(lineEnd);
}

std::optional<Lyrics> PlainOf(const QStringList& lines) {
    Lyrics lyrics{.kind = LyricsKind::Plain};
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (IdTagOf(line)) {
            continue;
        }
        if (!line.isEmpty()) {
            lyrics.lines << LyricsLine{.text = Cut(line)};
        } else if (!lyrics.lines.isEmpty() && !lyrics.lines.constLast().text.isEmpty()) {
            lyrics.lines << LyricsLine{};
        }
    }
    lyrics.lines.resize(std::min(lyrics.lines.size(), kMaxLyricsLines));
    if (!lyrics.lines.isEmpty() && lyrics.lines.constLast().text.isEmpty()) {
        lyrics.lines.removeLast();
    }
    if (lyrics.lines.isEmpty()) {
        return std::nullopt;
    }
    return lyrics;
}

}  // namespace

std::optional<Lyrics> ParseLyricsText(QStringView text) {
    const std::optional<QStringList> lines = LinesOf(text);
    if (!lines) {
        return std::nullopt;
    }
    static const QRegularExpression wordTag(QStringLiteral("<\\d+:\\d{2}(?:[.:]\\d+)?>"));
    std::optional<qint64> offset;
    QList<Timed> timed;
    for (const QString& raw : *lines) {
        const QString line = raw.trimmed();
        QList<qint64> times;
        qsizetype at = 0;
        while (const std::optional<TimeTag> tag = TimeTagAt(line, at)) {
            times << tag->ms;
            at += tag->length;
        }
        if (!times.isEmpty()) {
            const QString words = Cut(line.mid(at).remove(wordTag).trimmed());
            for (qint64 ms : times) {
                timed << Timed{ms, words};
            }
            continue;
        }
        const std::optional<std::pair<QString, QString>> idTag = IdTagOf(line);
        if (idTag && idTag->first == u"offset" && !offset) {
            bool ok = false;
            const qint64 value = idTag->second.toLongLong(&ok);
            if (ok) {
                offset = std::clamp(value, -kMaxOffsetMs, kMaxOffsetMs);
            }
        }
    }
    if (timed.isEmpty()) {
        return PlainOf(*lines);
    }
    for (Timed& line : timed) {
        line.ms = std::max<qint64>(0, line.ms - offset.value_or(0));
    }
    std::stable_sort(timed.begin(), timed.end(), [](const Timed& a, const Timed& b) { return a.ms < b.ms; });
    Lyrics lyrics{.kind = LyricsKind::Synced};
    for (const Timed& line : timed) {
        if (lyrics.lines.size() == kMaxLyricsLines) {
            break;
        }
        lyrics.lines << LyricsLine{line.ms, line.text};
    }
    return lyrics;
}

std::optional<Lyrics> PlainLyrics(QStringView text) {
    const std::optional<QStringList> lines = LinesOf(text);
    return lines ? PlainOf(*lines) : std::nullopt;
}

}  // namespace Core
