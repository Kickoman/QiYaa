#pragma once

#include "core/continuation.h"
#include "core/track.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

namespace Core {

struct QueueSource {
    QString title{};
    QList<Track> head{};
    std::shared_ptr<Continuation> continuation{};
    QList<Track> given{};
    bool waveFromHistory = true;
    std::shared_ptr<Continuation> historyWave{};
    std::optional<int> start{};
};

enum class AddPlacement { Next, End };

enum class EntryPart { Head, Continuation, Item, Other };

struct QueueEntry {
    Track track{};
    EntryPart part = EntryPart::Head;
    std::shared_ptr<Continuation> giver{};
    bool fromHistory = false;
    std::optional<int> seedsVersion{};
    QString itemId{};
    QString addedBy{};
    bool pending = false;
};

struct LayerItem {
    QString itemId{};
    Track track{};
    QString addedBy{};
};

struct QueueRules {
    bool playReports = true;
    bool previousRestartsOnly = false;
    bool ownContinuationFeedback = true;
    bool historyFeedback = true;
};

struct QueueLayer {
    QueueRules rules{};
    std::function<void(const QStringList& itemIds)> removeItems{};
};

}  // namespace Core
