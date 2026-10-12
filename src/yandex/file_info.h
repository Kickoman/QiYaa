#pragma once

#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

namespace Yandex {

// How a track's file is fetched (spec/player/quality.md).
enum class Quality { Standard, Economy };

// The stored name: "standard", "economy". Any other name is nullopt.
QString QualityKey(Quality quality);
std::optional<Quality> QualityFromKey(const QString& key);

// The `X-Yandex-Music-Client` the get-file-info signature is checked against.
inline constexpr char kFileInfoClient[] = "YandexMusicDesktopAppWindows/5.95.0";

struct FileInfoRequest {
    QString trackId;
    QString quality;
    QStringList codecs;
    QString transport;
};

struct FileInfo {
    QString quality;
    QString codec;
    int bitrateKbps = 0;
    QString transport;
    QUrl url;
    QStringList urls;
    QString key;
};

// `timestamp` is in Unix seconds.
QString SignFileInfo(const FileInfoRequest& request, qint64 timestamp);
// The percent-encoded query of GET /get-file-info, signature included.
QString FileInfoQuery(const FileInfoRequest& request, qint64 timestamp);

// The reply's `result`; nullopt without an https `url`.
std::optional<FileInfo> ParseFileInfo(const QJsonValue& result);

// A reply the app can play: a plain file in one of the codecs asked for.
bool IsUsable(const FileInfo& info, const FileInfoRequest& request);

}  // namespace Yandex
