#pragma once

#include "core/failure_policy.h"
#include "core/track_source.h"
#include "yandex/api_client.h"
#include "yandex/file_info.h"
#include "yandex/library.h"

namespace YandexSource {

class Source : public Core::TrackSource {
    Q_OBJECT
public:
    explicit Source(Yandex::Library* library, QObject* parent = nullptr);

    QUrl coverUrl(const Core::Track& track, int pixels) const override;
    QUrl webPage(const Core::Track& track) const override;
    void open(const Core::Track& track, TOpened done) override;
    void trackEvent(Core::TrackEvent event, const Core::Track& track, double playedSeconds) override;

    void setQuality(Yandex::Quality quality);
    Yandex::Quality quality() const { return fileQuality; }

private:
    Yandex::Library* yandexLibrary;
    Yandex::Quality fileQuality = Yandex::Quality::Standard;
};

Core::FailureKind FailureKindOf(const Yandex::RequestError& error);

}  // namespace YandexSource
