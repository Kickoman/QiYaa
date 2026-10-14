#pragma once

#include "core/continuation.h"
#include "sources/yandex/wave_origins.h"

#include <QString>

#include <memory>

namespace Yandex {
class Library;
struct WaveBatch;
}  // namespace Yandex

namespace YandexSource {

class Vibe : public Core::Continuation, public std::enable_shared_from_this<Vibe> {
public:
    Vibe(Yandex::Library* library, const Yandex::WaveBatch& first, QString station);

    void begin(bool feedback) override;
    bool more(const Core::ContinuationRequest& request, TDone done) override;
    void trackEvent(Core::TrackEvent event, const Core::Track& track, double playedSeconds) override;

private:
    Yandex::Library* yandexLibrary;
    QString session;
    QString station;
    QString firstBatch;
    bool began = false;
    WaveOrigins origins;
};

}  // namespace YandexSource
