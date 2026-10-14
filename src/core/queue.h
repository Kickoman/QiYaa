#pragma once

#include "core/continuation.h"
#include "core/queue_source.h"
#include "core/track.h"
#include "core/track_navigator.h"

#include <QList>
#include <QString>
#include <QtGlobal>

#include <memory>
#include <optional>

namespace Core {

class Queue {
public:
    const QList<QueueEntry>& entries() const { return queued; }
    const QString& title() const { return titleText; }
    int cursor() const { return playingIndex; }
    const QueueEntry* currentEntry() const;
    const std::shared_ptr<Continuation>& continuation() const { return ownContinuation; }
    quint64 revision() const { return changes; }

    void replace(QueueSource source);
    void replaceBase(QueueSource source);
    bool appendContinuation(
        const QList<Track>& tracks,
        const std::shared_ptr<Continuation>& giver,
        std::optional<int> seedsVersion
    );
    bool remove(QList<int> indices);
    void setItems(const QList<LayerItem>& items, std::optional<SeedHint> hint);
    void endLayer();
    bool isStale(int seedsVersion) const { return !seedHint || seedsVersion != seedHint->version; }

    int select(int index);
    int next() const;
    int previous() const;
    int remaining() const;

    void setShuffle(bool on);
    bool shuffle() const { return shuffleEnabled; }
    bool shuffleActive() const { return shuffleEnabled && !ownContinuation; }
    void setShuffleAlgorithm(ShuffleAlgorithm algorithm);
    ShuffleAlgorithm shuffleAlgorithm() const { return preferredAlgorithm; }
    void setRepeat(bool on) { repeatEnabled = on; }
    bool repeat() const { return repeatEnabled; }

    quint64 generation() const { return loadGeneration; }
    void renew();
    bool isAsking() const { return asking; }
    void setAsking(bool on) { asking = on; }

private:
    int ordinalOf(int index) const;
    int indexOf(EntryPart part, int ordinal) const;
    int countOf(EntryPart part) const;
    void markCurrent();
    void gatherPending();
    void dropStaleWave();
    void resetNavigator();

    QList<QueueEntry> queued;
    QString titleText;
    std::shared_ptr<Continuation> ownContinuation;
    int playingIndex = -1;
    bool inHead = false;
    int headMark = -1;
    int continuationMark = -1;
    std::optional<SeedHint> seedHint;
    std::unique_ptr<TrackNavigator> navigator = MakeTrackNavigator(std::nullopt);
    ShuffleAlgorithm preferredAlgorithm = ShuffleAlgorithm::WithoutRepeats;
    bool shuffleEnabled = false;
    bool repeatEnabled = false;
    quint64 loadGeneration = 0;
    bool asking = false;
    quint64 changes = 0;
};

}  // namespace Core
