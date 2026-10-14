#pragma once

#include "core/track.h"

#include <QHash>
#include <QString>

namespace Yandex {
class Library;
struct WaveBatch;
}  // namespace Yandex

namespace YandexSource {

class WaveOrigins {
public:
    void add(const QString& session, const QString& station, const Yandex::WaveBatch& batch);
    void send(Yandex::Library* library, Core::TrackEvent event, const Core::Track& track, double playedSeconds) const;

private:
    struct Origin {
        QString session;
        QString station;
        QString batch;
    };

    QHash<QString, Origin> originOfTrack;
};

}  // namespace YandexSource
