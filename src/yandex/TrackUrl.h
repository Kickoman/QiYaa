// Building a direct mp3 link from Yandex Music's download-info.
#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>

namespace qiyaa::yandex {

// One entry of GET /tracks/{id}/download-info.
struct DownloadVariant {
    QString codec;
    int bitrateKbps = 0;
    bool preview = false;
    QUrl downloadInfoUrl;
};

// Result of GET {downloadInfoUrl}&format=json.
struct DownloadInfo {
    QString host;
    QString path;
    QString ts;
    QString s;
};

QList<DownloadVariant> parseDownloadVariants(const QJsonArray& result);

// Best full (non-preview) mp3; falls back to the first entry. Returns false if empty.
bool pickBestVariant(const QList<DownloadVariant>& variants, DownloadVariant* out);

// Parses the JSON body of the download-info XML/JSON endpoint.
bool parseDownloadInfo(const QByteArray& json, DownloadInfo* out);

// https://{host}/get-mp3/{md5(SALT + path[1:] + s)}/{ts}{path}
QUrl buildTrackUrl(const DownloadInfo& info);

}  // namespace qiyaa::yandex
