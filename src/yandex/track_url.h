// Building a direct mp3 link from Yandex Music's download-info.
#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>

#include <optional>

namespace Yandex {

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

QList<DownloadVariant> ParseDownloadVariants(const QJsonArray& result);

// Best full (non-preview) mp3; falls back to the first entry. nullopt when empty.
std::optional<DownloadVariant> PickBestVariant(const QList<DownloadVariant>& variants);

// The JSON body of the download-info endpoint; nullopt when it isn't usable.
std::optional<DownloadInfo> ParseDownloadInfo(const QByteArray& json);

// https://{host}/get-mp3/{md5(SALT + path[1:] + s)}/{ts}{path}
QUrl BuildTrackUrl(const DownloadInfo& info);

}  // namespace Yandex
