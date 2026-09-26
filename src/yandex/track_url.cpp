#include "yandex/track_url.h"

#include <QCryptographicHash>
#include <QJsonDocument>

namespace Yandex {

namespace {
constexpr char kSignSalt[] = "XGRlBW9FXlekgbPrRHuSiA";
}

QList<DownloadVariant> ParseDownloadVariants(const QJsonArray& result) {
    QList<DownloadVariant> out;
    for (const QJsonValue& value : result) {
        const QJsonObject object = value.toObject();
        DownloadVariant d;
        d.codec = object.value(QStringLiteral("codec")).toString();
        d.bitrateKbps = object.value(QStringLiteral("bitrateInKbps")).toInt();
        d.preview = object.value(QStringLiteral("preview")).toBool();
        d.downloadInfoUrl = QUrl(object.value(QStringLiteral("downloadInfoUrl")).toString());
        if (d.downloadInfoUrl.isValid()) {
            out.append(d);
        }
    }
    return out;
}

bool PickBestVariant(const QList<DownloadVariant>& variants, DownloadVariant* out) {
    const DownloadVariant* best = nullptr;
    for (const DownloadVariant& v : variants) {
        if (v.codec != QLatin1String("mp3") || v.preview) {
            continue;
        }
        if (!best || v.bitrateKbps > best->bitrateKbps) {
            best = &v;
        }
    }
    if (!best && !variants.isEmpty()) {
        best = &variants.first();
    }
    if (!best) {
        return false;
    }
    *out = *best;
    return true;
}

bool ParseDownloadInfo(const QByteArray& json, DownloadInfo* out) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    const QJsonObject object = doc.object();
    auto str = [&](const char* key) {
        const QJsonValue value = object.value(QLatin1String(key));
        return value.isString() ? value.toString()
            : value.isDouble()  ? QString::number(qint64(value.toDouble()))
                                : QString();
    };
    out->host = str("host");
    out->path = str("path");
    out->ts = str("ts");
    out->s = str("s");
    return !out->host.isEmpty() && out->path.startsWith(u'/') && !out->s.isEmpty();
}

QUrl BuildTrackUrl(const DownloadInfo& info) {
    const QByteArray toSign = QByteArray(kSignSalt) + info.path.mid(1).toUtf8() + info.s.toUtf8();
    const QByteArray sign = QCryptographicHash::hash(toSign, QCryptographicHash::Md5).toHex();
    return QUrl(QStringLiteral("https://%1/get-mp3/%2/%3%4")
                    .arg(info.host, QString::fromLatin1(sign), info.ts, info.path));
}

}  // namespace Yandex
