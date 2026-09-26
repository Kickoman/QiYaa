#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>

#include <optional>

namespace Yandex {

struct DownloadVariant {
    QString codec;
    int bitrateKbps = 0;
    bool preview = false;
    QUrl downloadInfoUrl;
};

struct DownloadInfo {
    QString host;
    QString path;
    QString ts;
    QString s;
};

QList<DownloadVariant> ParseDownloadVariants(const QJsonArray& result);

std::optional<DownloadVariant> PickBestVariant(const QList<DownloadVariant>& variants);

std::optional<DownloadInfo> ParseDownloadInfo(const QByteArray& json);

QUrl BuildTrackUrl(const DownloadInfo& info);

}  // namespace Yandex
