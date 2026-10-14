#pragma once

#include "core/track_source.h"
#include "sources/yandex/source.h"
#include "sources/yandex/tracks.h"
#include "yandex/library.h"

namespace Tests {

struct YandexSources {
    explicit YandexSources(Yandex::Library* library)
        : source(library) {
        registry.add(YandexSource::kSourceName, &source);
    }

    YandexSource::Source source;
    Core::SourceRegistry registry;
};

}  // namespace Tests
