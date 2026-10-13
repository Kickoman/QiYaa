#pragma once

#include "core/player.h"
#include "core/track.h"
#include "yandex/api_client.h"
#include "yandex/library.h"

#include <QList>
#include <QString>

#include <optional>

namespace YandexSource {

inline const QString kSourceName = QStringLiteral("yandex");

Core::TrackKey KeyOf(const QString& trackId, const QString& albumId);
Core::Track TrackOf(const Yandex::Track& track);
QList<Core::Track> TracksOf(const QList<Yandex::Track>& tracks);
std::optional<Yandex::Track> YandexTrackOf(const Core::Track& track);
QString CatalogId(const Core::TrackKey& key);

Yandex::WaveEvent WaveEventOf(Core::Player::TrackEvent event);

}  // namespace YandexSource
