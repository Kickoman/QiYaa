#include "core/queue.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <utility>

namespace Core {

namespace {
QList<QueueEntry> EntriesOf(
    const QList<Track>& tracks,
    EntryPart part,
    const std::shared_ptr<Continuation>& giver,
    bool fromHistory = false,
    std::optional<int> seedsVersion = std::nullopt
) {
    QList<QueueEntry> entries;
    for (const Track& track : tracks) {
        if (track.available) {
            entries << QueueEntry{
                .track = track, .part = part, .giver = giver, .fromHistory = fromHistory, .seedsVersion = seedsVersion
            };
        }
    }
    return entries;
}

bool IsPending(const QueueEntry& entry) {
    return entry.part == EntryPart::Item && entry.pending;
}

bool IsWaitingItem(const QueueEntry& entry) {
    return IsPending(entry) && !entry.itemId.isEmpty();
}
}  // namespace

const QueueEntry* Queue::currentEntry() const {
    return (playingIndex >= 0 && playingIndex < queued.size()) ? &queued[playingIndex] : nullptr;
}

void Queue::replace(QueueSource source) {
    ownContinuation = std::move(source.continuation);
    historyAllowed = source.waveFromHistory;
    queued = EntriesOf(source.head, EntryPart::Head, nullptr);
    queued << EntriesOf(source.given, EntryPart::Continuation, ownContinuation);
    titleText = source.title;
    playingIndex = queued.isEmpty() ? -1 : 0;
    newBase();
    markCurrent();
    resetNavigator();
    ++changes;
}

void Queue::replaceBase(QueueSource source) {
    QList<QueueEntry> kept;
    if (const QueueEntry* entry = currentEntry()) {
        kept << *entry;
        if (entry->part != EntryPart::Item) {
            kept.front().part = EntryPart::Other;
        }
    }
    std::copy_if(queued.cbegin(), queued.cend(), std::back_inserter(kept), IsWaitingItem);  // HOST-21, HOST-36
    playingIndex = currentEntry() ? 0 : -1;
    ownContinuation = std::move(source.continuation);
    historyAllowed = source.waveFromHistory;
    kept << EntriesOf(source.head, EntryPart::Head, nullptr);
    kept << EntriesOf(source.given, EntryPart::Continuation, ownContinuation);
    queued = std::move(kept);
    titleText = source.title;
    newBase();
    resetNavigator();
    ++changes;
}

bool Queue::appendContinuation(
    const QList<Track>& tracks,
    const std::shared_ptr<Continuation>& giver,
    std::optional<int> seedsVersion
) {
    const bool fromHistory = giver && giver == historyContinuation;
    const QList<QueueEntry> added = EntriesOf(tracks, EntryPart::Continuation, giver, fromHistory, seedsVersion);
    if (added.isEmpty()) {
        return false;
    }
    queued << added;
    historyGave = historyGave || fromHistory;
    if (playingIndex < 0 && queued.size() == added.size()) {
        playingIndex = 0;
        markCurrent();
    }
    ++changes;
    return true;
}

bool Queue::remove(QList<int> indices) {
    std::sort(indices.begin(), indices.end(), std::greater<>());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    bool removedCurrent = false;
    bool headChanged = false;
    for (int index : indices) {
        if (index < 0 || index >= queued.size()) {
            continue;
        }
        const int ordinal = ordinalOf(index);
        if (queued[index].part == EntryPart::Head) {
            headChanged = true;
            headMark -= ordinal <= headMark ? 1 : 0;
        } else if (queued[index].part == EntryPart::Continuation) {
            continuationMark -= ordinal <= continuationMark ? 1 : 0;
        }
        queued.removeAt(index);
        ++changes;
        if (index == playingIndex) {
            removedCurrent = true;
        } else if (index < playingIndex) {
            --playingIndex;
        }
    }
    if (queued.isEmpty()) {
        playingIndex = -1;
    } else if (removedCurrent) {
        playingIndex = std::min<int>(playingIndex, static_cast<int>(queued.size()) - 1);
        if (IsPending(queued[playingIndex])) {
            playingIndex = -1;  // HOST-36: the next item waits until it starts
        } else {
            markCurrent();
        }
    }
    if (headChanged) {
        resetNavigator();
    }
    return removedCurrent;
}

void Queue::setItems(const QList<LayerItem>& items, std::optional<SeedHint> hint) {
    if (hint && !hint->seeds.isEmpty() && (!seedHint || seedHint->version != hint->version)) {
        stalled = false;  // HOST-11: new seeds
    }
    seedHint = std::move(hint);
    const QueueEntry* current = currentEntry();
    const bool currentIsItem = current && current->part == EntryPart::Item;
    QList<LayerItem> wanted;
    for (const LayerItem& item : items) {
        if (!currentIsItem || item.itemId != current->itemId) {
            wanted << item;  // HOST-04: the current item never goes back into the tail
        }
    }
    const auto firstPending = std::find_if(queued.cbegin(), queued.cend(), IsPending);
    const int start =
        firstPending != queued.cend() ? static_cast<int>(firstPending - queued.cbegin()) : playingIndex + 1;
    int end = start;
    while (end < queued.size() && IsWaitingItem(queued[end])) {
        ++end;  // HOST-12, HOST-32: items left from an earlier jam are the base's and wait behind
    }
    int prefix = 0;
    while (start + prefix < end && prefix < wanted.size() && queued[start + prefix].itemId == wanted[prefix].itemId) {
        ++prefix;  // HOST-01, HOST-02: the common prefix stays, with its preload
    }
    if (start + prefix < end) {
        queued.remove(start + prefix, end - start - prefix);
        ++changes;
    }
    for (qsizetype i = prefix; i < wanted.size(); ++i) {
        Track track = wanted[i].track;
        track.available = true;
        queued.insert(
            start + i,
            QueueEntry{
                .track = track,
                .part = EntryPart::Item,
                .itemId = wanted[i].itemId,
                .addedBy = wanted[i].addedBy,
                .pending = true
            }
        );
        ++changes;
    }
    dropStaleWave();
}

void Queue::endLayer() {
    seedHint.reset();
    QList<int> stale;
    for (int i = 0; i < queued.size(); ++i) {
        QueueEntry& entry = queued[i];
        entry.itemId.clear();
        if (entry.part == EntryPart::Continuation && entry.seedsVersion && ordinalOf(i) > continuationMark) {
            stale << i;  // HOST-32: the jam's own seeds are over
        }
    }
    remove(stale);
    ++changes;
}

bool Queue::isStale(std::optional<int> seedsVersion) const {
    if (!seedHint) {
        return seedsVersion.has_value();
    }
    return !seedHint->seeds.isEmpty() && seedsVersion != seedHint->version;
}

int Queue::select(int index) {
    if (index < 0 || index >= queued.size()) {
        return playingIndex;
    }
    if (IsPending(queued[index])) {
        ++changes;
    }
    playingIndex = index;
    markCurrent();
    gatherPending();
    dropStaleWave();
    return playingIndex;
}

int Queue::next() const {
    const auto pending = std::find_if(queued.cbegin(), queued.cend(), IsPending);
    if (pending != queued.cend()) {
        return static_cast<int>(pending - queued.cbegin());
    }
    if (inHead) {
        const int ordinal = navigator->next(repeatEnabled && !ownContinuation);
        if (ordinal >= 0) {
            return indexOf(EntryPart::Head, ordinal);
        }
    }
    return indexOf(EntryPart::Continuation, continuationMark + 1);
}

int Queue::previous() const {
    const QueueEntry* entry = currentEntry();
    if (entry && entry->part == EntryPart::Head) {
        return indexOf(EntryPart::Head, navigator->previous(repeatEnabled));
    }
    const int row = std::max(0, playingIndex - 1);
    if (row != playingIndex && row < queued.size() && queued[row].part == EntryPart::Head && headMark >= 0) {
        return indexOf(EntryPart::Head, headMark);  // SHUF-02: the head's last track in play order
    }
    return row;
}

int Queue::stepBack() {
    const int target = previous();
    const QueueEntry* entry = currentEntry();
    if (entry && entry->part == EntryPart::Continuation && target >= 0
        && queued[target].part != EntryPart::Continuation) {
        continuationMark = ordinalOf(playingIndex) - 1;
    }
    return target;
}

int Queue::firstToPlay() const {
    return playingIndex >= 0 ? playingIndex : std::max(0, next());
}

int Queue::remaining() const {
    int count = currentEntry() ? 1 : 0;
    count += static_cast<int>(std::count_if(queued.cbegin(), queued.cend(), IsPending));
    if (inHead) {
        const int head = navigator->remaining(repeatEnabled && !ownContinuation);
        if (head == kEndlessTracks) {
            return kEndlessTracks;
        }
        count += head;
    }
    return count + std::max(0, countOf(EntryPart::Continuation) - continuationMark - 1);
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

void Queue::noteStarted(const QueueEntry& entry) {
    if (entry.part == EntryPart::Continuation) {
        return;  // CONT-05
    }
    startedTracks.removeIf([&entry](const Track& track) { return track.key == entry.track.key; });
    startedTracks.prepend(entry.track);
}

void Queue::renew() {
    ++loadGeneration;
    asking = false;
}

int Queue::ordinalOf(int index) const {
    const EntryPart part = queued[index].part;
    return static_cast<int>(std::count_if(queued.cbegin(), queued.cbegin() + index, [part](const QueueEntry& entry) {
        return entry.part == part;
    }));
}

int Queue::indexOf(EntryPart part, int ordinal) const {
    if (ordinal < 0) {
        return -1;
    }
    for (int i = 0; i < queued.size(); ++i) {
        if (queued[i].part == part && ordinal-- == 0) {
            return i;
        }
    }
    return -1;
}

int Queue::countOf(EntryPart part) const {
    return static_cast<int>(std::count_if(queued.cbegin(), queued.cend(), [part](const QueueEntry& entry) {
        return entry.part == part;
    }));
}

void Queue::markCurrent() {
    if (!currentEntry()) {
        return;
    }
    QueueEntry& entry = queued[playingIndex];
    switch (entry.part) {
        case EntryPart::Head:
            inHead = true;
            headMark = ordinalOf(playingIndex);
            navigator->select(headMark);
            break;
        case EntryPart::Continuation:
            inHead = false;
            continuationMark = ordinalOf(playingIndex);
            break;
        case EntryPart::Item: entry.pending = false; break;
        case EntryPart::Other: break;
    }
}

void Queue::gatherPending() {
    QList<int> pending;
    for (int i = 0; i < queued.size(); ++i) {
        if (IsPending(queued[i])) {
            pending << i;
        }
    }
    bool inPlace = true;
    for (qsizetype k = 0; k < pending.size() && inPlace; ++k) {
        inPlace = pending[k] == playingIndex + 1 + k;
    }
    if (inPlace) {
        return;
    }
    QList<QueueEntry> moved;
    for (qsizetype k = pending.size() - 1; k >= 0; --k) {
        moved.prepend(queued.takeAt(pending[k]));
        playingIndex -= pending[k] < playingIndex ? 1 : 0;
    }
    for (qsizetype k = 0; k < moved.size(); ++k) {
        queued.insert(playingIndex + 1 + k, moved[k]);
    }
    ++changes;
}

void Queue::dropStaleWave() {
    const QueueEntry* entry = currentEntry();
    if (!seedHint || seedHint->seeds.isEmpty() || (entry && entry->fromHistory)
        || std::any_of(queued.cbegin(), queued.cend(), IsPending)) {
        return;
    }
    QList<int> stale;
    for (int i = 0; i < queued.size(); ++i) {
        const QueueEntry& candidate = queued[i];
        if (candidate.part == EntryPart::Continuation && candidate.fromHistory
            && candidate.seedsVersion != seedHint->version && ordinalOf(i) > continuationMark) {
            stale << i;  // HOST-11
        }
    }
    remove(stale);
}

void Queue::newBase() {
    inHead = countOf(EntryPart::Head) > 0;
    headMark = -1;
    continuationMark = -1;
    historyContinuation.reset();
    historyGave = false;
    startedTracks.clear();
    stalled = false;
    renew();
}

void Queue::resetNavigator() {
    navigator = MakeTrackNavigator(shuffleActive() ? std::optional(preferredAlgorithm) : std::nullopt);
    navigator->reset(countOf(EntryPart::Head), headMark);
}

}  // namespace Core
