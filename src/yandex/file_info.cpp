#include "yandex/file_info.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageAuthenticationCode>

#include <utility>

namespace Yandex {

namespace {
constexpr char kSignKey[] = "kzqU4XhfCaY6B6JTHODeq5";  // Yandex Music's Windows app
}  // namespace

QString QualityKey(Quality quality) {
    switch (quality) {
        case Quality::Standard: return QStringLiteral("standard");
        case Quality::Economy: return QStringLiteral("economy");
    }
    return QStringLiteral("standard");
}

std::optional<Quality> QualityFromKey(const QString& key) {
    for (const Quality quality : {Quality::Standard, Quality::Economy}) {
        if (key == QualityKey(quality)) {
            return quality;
        }
    }
    return std::nullopt;
}

QString SignFileInfo(const FileInfoRequest& request, qint64 timestamp) {
    const QString message = QString::number(timestamp) + request.trackId + request.quality
        + request.codecs.join(QString()) + request.transport;
    const QByteArray signature =
        QMessageAuthenticationCode::hash(
            message.toUtf8(), QByteArray(kSignKey), QCryptographicHash::Sha256
        )
            .toBase64();
    return QString::fromLatin1(signature.chopped(1));
}

QString FileInfoQuery(const FileInfoRequest& request, qint64 timestamp) {
    const std::pair<const char*, QString> items[] = {
        {"ts", QString::number(timestamp)}, {"trackId", request.trackId},
        {"quality", request.quality},       {"codecs", request.codecs.join(u',')},
        {"transports", request.transport},  {"sign", SignFileInfo(request, timestamp)},
    };
    QByteArray query;
    for (const auto& [name, value] : items) {
        if (!query.isEmpty()) {
            query += '&';
        }
        query += QByteArray(name) + '=' + QUrl::toPercentEncoding(value);
    }
    return QString::fromLatin1(query);
}

std::optional<FileInfo> ParseFileInfo(const QJsonValue& result) {
    const QJsonObject object = result.toObject().value(QStringLiteral("downloadInfo")).toObject();
    FileInfo info;
    info.url = QUrl(object.value(QStringLiteral("url")).toString());
    if (!info.url.isValid() || info.url.scheme() != QLatin1String("https")
        || info.url.host().isEmpty()) {
        return std::nullopt;
    }
    info.quality = object.value(QStringLiteral("quality")).toString();
    info.codec = object.value(QStringLiteral("codec")).toString();
    info.bitrateKbps = object.value(QStringLiteral("bitrate")).toInt();
    info.transport = object.value(QStringLiteral("transport")).toString();
    for (const QJsonValue& mirror : object.value(QStringLiteral("urls")).toArray()) {
        info.urls << mirror.toString();
    }
    info.key = object.value(QStringLiteral("key")).toString();
    return info;
}

bool IsUsable(const FileInfo& info, const FileInfoRequest& request) {
    return info.transport == QLatin1String("raw") && request.codecs.contains(info.codec);
}

}  // namespace Yandex
