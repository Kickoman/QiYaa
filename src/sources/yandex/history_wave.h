#pragma once

#include "core/continuation.h"
#include "core/track.h"
#include "sources/yandex/wave_origins.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

namespace Yandex {
class Library;
}  // namespace Yandex

namespace YandexSource {

inline constexpr int kHistorySeeds = 5;

QStringList HistorySeeds(const QList<Core::Track>& history);

class HistoryWave : public Core::Continuation, public std::enable_shared_from_this<HistoryWave> {
public:
    explicit HistoryWave(Yandex::Library* library);

    bool more(const Core::ContinuationRequest& request, TDone done) override;
    void trackEvent(Core::TrackEvent event, const Core::Track& track, double playedSeconds) override;

private:
    void start(const QStringList& seeds, bool feedback, TDone done);

    Yandex::Library* yandexLibrary;
    QString session;
    QString station;
    WaveOrigins origins;
};

Core::THistoryWaveFactory HistoryWaves(Yandex::Library* library);

}  // namespace YandexSource
