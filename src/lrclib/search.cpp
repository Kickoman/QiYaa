#include "lrclib/search.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QRegularExpressionMatch>

#include <cmath>

namespace Lrclib {

namespace {

bool Fits(const Record& record, qint64 durationMs) {
    return durationMs <= 0
        || std::abs(record.durationSeconds * 1000 - static_cast<double>(durationMs)) <= kDurationToleranceMs;
}

Choice Found(const Core::Lyrics& lyrics, qint64 id) {
    return {{.status = Core::LyricsAnswer::Status::Found, .lyrics = lyrics, .from = Core::LyricsFrom::ThirdParty}, id};
}

}  // namespace

QList<Record> ParseSearch(const QByteArray& body) {
    QList<Record> records;
    for (const QJsonValue& value : QJsonDocument::fromJson(body).array()) {
        const QJsonObject object = value.toObject();
        records << Record{
            .id = object.value(QStringLiteral("id")).toInteger(),
            .durationSeconds = object.value(QStringLiteral("duration")).toDouble(),
            .instrumental = object.value(QStringLiteral("instrumental")).toBool(),
            .plainLyrics = object.value(QStringLiteral("plainLyrics")).toString(),
            .syncedLyrics = object.value(QStringLiteral("syncedLyrics")).toString(),
        };
    }
    return records;
}

Choice Pick(const QList<Record>& records, qint64 durationMs) {
    QList<Record> candidates;
    for (const Record& record : records) {
        if (Fits(record, durationMs)) {
            candidates << record;
        }
    }
    for (const Record& record : candidates) {
        const std::optional<Core::Lyrics> synced = Core::ParseLyricsText(record.syncedLyrics);
        if (synced && synced->kind == Core::LyricsKind::Synced) {
            return Found(*synced, record.id);
        }
    }
    for (const Record& record : candidates) {
        std::optional<Core::Lyrics> plain = Core::PlainLyrics(record.plainLyrics);
        if (!plain) {
            plain = Core::ParseLyricsText(record.syncedLyrics);
        }
        if (plain) {
            return Found(*plain, record.id);
        }
    }
    for (const Record& record : candidates) {
        if (record.instrumental) {
            return {
                {.status = Core::LyricsAnswer::Status::Instrumental, .from = Core::LyricsFrom::ThirdParty}, record.id
            };
        }
    }
    return {{.from = Core::LyricsFrom::ThirdParty}, std::nullopt};
}

QString TitleWithoutVersion(const QString& title) {
    static const QRegularExpression version(QStringLiteral("^(.*\\S)\\s+(?:\\([^()]*\\)|\\[[^\\[\\]]*\\])\\s*$"));
    const QRegularExpressionMatch match = version.match(title);
    return match.hasMatch() ? match.captured(1) : QString();
}

}  // namespace Lrclib
