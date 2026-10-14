#pragma once

#include "core/continuation.h"
#include "core/track.h"
#include "sources/yandex/wave_origins.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace Yandex {
class Library;
}  // namespace Yandex

namespace YandexSource {

inline constexpr int kHistorySeeds = 5;

QStringList HistorySeeds(const QList<Core::Track>& history);

class HistoryWave : public Core::Continuation, public std::enable_shared_from_this<HistoryWave> {
public:
    explicit HistoryWave(Yandex::Library* library);

    void begin(bool feedback) override;
    bool more(const Core::ContinuationRequest& request, TDone done) override;
    void trackEvent(Core::TrackEvent event, const Core::Track& track, double playedSeconds) override;

private:
    bool goesOn(const Core::ContinuationRequest& request) const;
    void start(const QStringList& seeds, std::optional<int> seedsVersion, bool feedback, TDone done);

    Yandex::Library* yandexLibrary;
    QString session;
    QString station;
    QString firstBatch;
    std::optional<int> sessionSeedsVersion;
    bool radioStarted = false;
    int starts = 0;
    WaveOrigins origins;
};

Core::THistoryWaveFactory HistoryWaves(Yandex::Library* library);

}  // namespace YandexSource
