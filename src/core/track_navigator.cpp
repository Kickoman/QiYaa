#include "core/track_navigator.h"

#include <QList>
#include <QRandomGenerator>

#include <algorithm>

namespace Core {
namespace {

class SequentialNavigator : public TrackNavigator {
public:
    void reset(int size, int current) override {
        trackCount = size;
        select(current);
    }
    void resetBefore(int size, int first) override { reset(size, first - 1); }
    void append(int count) override { trackCount += count; }
    void select(int index) override { currentIndex = index; }
    int next(bool repeat) const override {
        if (trackCount == 0) {
            return -1;
        }
        if (currentIndex + 1 < trackCount) {
            return currentIndex + 1;
        }
        return repeat ? 0 : -1;
    }
    int previous(bool repeat) const override {
        if (trackCount == 0) {
            return -1;
        }
        if (currentIndex > 0) {
            return currentIndex - 1;
        }
        return repeat ? trackCount - 1 : 0;
    }
    int remaining(bool repeat) const override {
        if (trackCount == 0) {
            return 0;
        }
        return repeat ? kEndlessTracks : trackCount - currentIndex - 1;
    }

protected:
    int trackCount = 0;
    int currentIndex = -1;
};

class RandomNavigator final : public SequentialNavigator {
public:
    void resetBefore(int size, int first) override {
        reset(size, -1);
        nextIndex = trackCount < 2 ? -1 : first;
    }
    void append(int count) override {
        const bool hadChoice = trackCount >= 2;
        SequentialNavigator::append(count);
        if (!hadChoice) {
            select(currentIndex);
        }
    }
    void select(int index) override {
        SequentialNavigator::select(index);
        nextIndex = -1;
        if (trackCount < 2) {
            return;
        }
        if (index < 0) {
            nextIndex = static_cast<int>(QRandomGenerator::global()->bounded(trackCount));
            return;
        }
        nextIndex = static_cast<int>(QRandomGenerator::global()->bounded(trackCount - 1));
        if (nextIndex >= currentIndex) {
            ++nextIndex;
        }
    }
    int next(bool repeat) const override { return trackCount < 2 ? SequentialNavigator::next(repeat) : nextIndex; }
    int remaining(bool repeat) const override {
        return trackCount < 2 ? SequentialNavigator::remaining(repeat) : kEndlessTracks;
    }

private:
    int nextIndex = -1;
};

class ShuffledNavigator final : public TrackNavigator {
public:
    void reset(int size, int current) override {
        order.clear();
        position = -1;
        if (size == 0) {
            return;
        }
        order.reserve(size);
        if (current >= 0) {
            order << current;
            position = 0;
        }
        for (int index = 0; index < size; ++index) {
            if (index != current) {
                order << index;
            }
        }
        std::shuffle(order.begin() + position + 1, order.end(), *QRandomGenerator::global());
    }
    void resetBefore(int size, int first) override {
        reset(size, first);
        position = -1;
    }
    void append(int count) override {
        for (int added = 0; added < count; ++added) {
            const int last = static_cast<int>(order.size());
            order.insert(QRandomGenerator::global()->bounded(position + 1, last + 1), last);
        }
    }
    void select(int index) override { position = static_cast<int>(order.indexOf(index)); }
    int next(bool repeat) const override {
        if (order.isEmpty()) {
            return -1;
        }
        if (position + 1 < order.size()) {
            return order[position + 1];
        }
        return repeat ? order.front() : -1;
    }
    int previous(bool repeat) const override {
        if (order.isEmpty()) {
            return -1;
        }
        if (position > 0) {
            return order[position - 1];
        }
        return repeat ? order.back() : order.front();
    }
    int remaining(bool repeat) const override {
        if (order.isEmpty()) {
            return 0;
        }
        return repeat ? kEndlessTracks : static_cast<int>(order.size()) - position - 1;
    }

private:
    QList<int> order;
    int position = -1;
};

}  // namespace

std::unique_ptr<TrackNavigator> MakeTrackNavigator(std::optional<ShuffleAlgorithm> algorithm) {
    if (!algorithm) {
        return std::make_unique<SequentialNavigator>();
    }
    if (*algorithm == ShuffleAlgorithm::Random) {
        return std::make_unique<RandomNavigator>();
    }
    return std::make_unique<ShuffledNavigator>();
}

}  // namespace Core
