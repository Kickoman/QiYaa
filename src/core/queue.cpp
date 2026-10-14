#include "core/queue.h"

#include <algorithm>
#include <functional>
#include <utility>

namespace Core {

namespace {
QList<QueueEntry> EntriesOf(const QList<Track>& tracks, EntryPart part, const std::shared_ptr<Continuation>& giver) {
    QList<QueueEntry> entries;
    for (const Track& track : tracks) {
        if (track.available) {
            entries << QueueEntry{.track = track, .part = part, .giver = giver};
        }
    }
    return entries;
}
}  // namespace

const QueueEntry* Queue::currentEntry() const {
    return (playingIndex >= 0 && playingIndex < queued.size()) ? &queued[playingIndex] : nullptr;
}

void Queue::replace(QueueSource source) {
    ownContinuation = std::move(source.continuation);
    queued = EntriesOf(source.head, EntryPart::Head, nullptr);
    queued << EntriesOf(source.given, EntryPart::Continuation, ownContinuation);
    titleText = source.title;
    playingIndex = queued.isEmpty() ? -1 : 0;
    renew();
    resetNavigator();
}

bool Queue::appendContinuation(const QList<Track>& tracks, const std::shared_ptr<Continuation>& giver) {
    const QList<QueueEntry> added = EntriesOf(tracks, EntryPart::Continuation, giver);
    if (added.isEmpty()) {
        return false;
    }
    queued << added;
    if (playingIndex < 0) {
        playingIndex = 0;
    }
    resetNavigator();
    return true;
}

bool Queue::insert(int index, const QList<Track>& tracks) {
    const QList<QueueEntry> added = EntriesOf(tracks, EntryPart::Head, nullptr);
    if (added.isEmpty()) {
        return false;
    }
    const int at = std::clamp(index, 0, static_cast<int>(queued.size()));
    for (qsizetype i = 0; i < added.size(); ++i) {
        queued.insert(at + i, added[i]);
    }
    if (playingIndex < 0) {
        playingIndex = 0;
    } else if (at <= playingIndex) {
        playingIndex += static_cast<int>(added.size());
    }
    resetNavigator();
    return true;
}

bool Queue::remove(QList<int> indices) {
    std::sort(indices.begin(), indices.end(), std::greater<>());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    bool removedCurrent = false;
    for (int index : indices) {
        if (index < 0 || index >= queued.size()) {
            continue;
        }
        queued.removeAt(index);
        if (index == playingIndex) {
            removedCurrent = true;
        } else if (index < playingIndex) {
            --playingIndex;
        }
    }
    if (removedCurrent) {
        playingIndex = std::min<int>(playingIndex, static_cast<int>(queued.size()) - 1);
    }
    if (queued.isEmpty()) {
        playingIndex = -1;
    }
    resetNavigator();
    return removedCurrent;
}

void Queue::setContinuation(std::shared_ptr<Continuation> continuation) {
    ownContinuation = std::move(continuation);
    renew();
    resetNavigator();
}

void Queue::select(int index) {
    playingIndex = index;
    navigator->select(index);
}

int Queue::next() const {
    return navigator->next(repeatEnabled && !ownContinuation);
}

int Queue::previous() const {
    return navigator->previous(repeatEnabled);
}

void Queue::setShuffle(bool on) {
    shuffleEnabled = on;
    resetNavigator();
}

void Queue::setShuffleAlgorithm(ShuffleAlgorithm algorithm) {
    preferredAlgorithm = algorithm;
    if (shuffleActive()) {
        resetNavigator();
    }
}

void Queue::renew() {
    ++loadGeneration;
    asking = false;
}

void Queue::resetNavigator() {
    navigator = MakeTrackNavigator(shuffleActive() ? std::optional(preferredAlgorithm) : std::nullopt);
    navigator->reset(static_cast<int>(queued.size()), playingIndex);
}

}  // namespace Core
