#pragma once

#include "yandex/api_client.h"
#include "yandex/library.h"
#include "yandex/track_url.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace Tests {

// The app's models in the neutral form of spec/expected/yandex (see its README).

QJsonObject ToJson(const Yandex::Account& account);
QJsonObject ToJson(const Yandex::Track& track);
QJsonObject ToJson(const Yandex::WaveBatch& batch);
QJsonObject ToJson(const Yandex::SearchResult& result);
QJsonObject ToJson(const Yandex::DownloadInfo& info);

QJsonObject TracksJson(const QList<Yandex::Track>& tracks);  // {"tracks": [...]}
QJsonObject IdsJson(const QString& key, const QStringList& ids);
QJsonObject PlaylistsJson(const QList<Yandex::PlaylistReference>& playlists);
QJsonObject NamedJson(const QString& key, const QList<Yandex::NamedReference>& references);
QJsonObject StationsJson(const QList<Yandex::Station>& stations);
QJsonObject WavesJson(const QList<Yandex::Wave>& waves);
QJsonObject VariantsJson(
    const QList<Yandex::DownloadVariant>& variants,
    const std::optional<Yandex::DownloadVariant>& best
);

// Checks an error text against an expected {"error": {"status", "message"}}: the text must name
// the status ("HTTP 401") and contain the message unless it is null. Returns what is wrong, or
// an empty string.
QString CheckError(const QString& error, const QString& expectedName);

}  // namespace Tests
