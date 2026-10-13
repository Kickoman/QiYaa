#pragma once

#include "core/track_source.h"

namespace YandexSource {

class Source : public Core::TrackSource {
public:
    QUrl coverUrl(const Core::Track& track, int pixels) const override;
    QUrl webPage(const Core::Track& track) const override;
};

}  // namespace YandexSource
