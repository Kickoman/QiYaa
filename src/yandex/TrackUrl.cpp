#include "yandex/TrackUrl.h"

#include <QCryptographicHash>
#include <QJsonDocument>

namespace qiyaa::yandex {

namespace {
constexpr char kSignSalt[] = "XGRlBW9FXlekgbPrRHuSiA";
}

QList<DownloadVariant> parseDownloadVariants(const QJsonArray& result) {
    QList<DownloadVariant> out;
    for (const QJsonValue& v : result) {
        const QJsonObject o = v.toObject();
        DownloadVariant d;
        d.codec = o.value(QStringLiteral("codec")).toString();
        d.bitrateKbps = o.value(QStringLiteral("bitrateInKbps")).toInt();
        d.preview = o.value(QStringLiteral("preview")).toBool();
        d.downloadInfoUrl = QUrl(o.value(QStringLiteral("downloadInfoUrl")).toString());
        if (d.downloadInfoUrl.isValid()) out.append(d);
    }
    return out;
}

bool pickBestVariant(const QList<DownloadVariant>& variants, DownloadVariant* out) {
    const DownloadVariant* best = nullptr;
    for (const DownloadVariant& v : variants) {
        if (v.codec != QLatin1String("mp3") || v.preview) continue;
        if (!best || v.bitrateKbps > best->bitrateKbps) best = &v;
    }
    if (!best && !variants.isEmpty()) best = &variants.first();
    if (!best) return false;
    *out = *best;
    return true;
}

bool parseDownloadInfo(const QByteArray& json, DownloadInfo* out) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
    const QJsonObject o = doc.object();
    auto str = [&](const char* key) {
        const QJsonValue v = o.value(QLatin1String(key));
        return v.isString() ? v.toString() : v.isDouble() ? QString::number(qint64(v.toDouble())) : QString();
    };
    out->host = str("host");
    out->path = str("path");
    out->ts = str("ts");
    out->s = str("s");
    return !out->host.isEmpty() && out->path.startsWith(u'/') && !out->s.isEmpty();
}

QUrl buildTrackUrl(const DownloadInfo& info) {
    const QByteArray toSign = QByteArray(kSignSalt) + info.path.mid(1).toUtf8() + info.s.toUtf8();
    const QByteArray sign = QCryptographicHash::hash(toSign, QCryptographicHash::Md5).toHex();
    return QUrl(QStringLiteral("https://%1/get-mp3/%2/%3%4")
                    .arg(info.host, QString::fromLatin1(sign), info.ts, info.path));
}

}  // namespace qiyaa::yandex
