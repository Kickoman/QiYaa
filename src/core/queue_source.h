#pragma once

#include "core/continuation.h"
#include "core/track.h"

#include <QList>
#include <QString>

#include <memory>

namespace Core {

struct QueueSource {
    QString title{};
    QList<Track> head{};
    std::shared_ptr<Continuation> continuation{};
    QList<Track> given{};
};

enum class EntryPart { Head, Continuation };

struct QueueEntry {
    Track track{};
    EntryPart part = EntryPart::Head;
    std::shared_ptr<Continuation> giver{};
};

}  // namespace Core
