#pragma once

#include <memory>
#include <optional>

namespace Core {

enum class ShuffleAlgorithm { Random, WithoutRepeats };

class TrackNavigator {
public:
    virtual ~TrackNavigator() = default;
    virtual void reset(int size, int current) = 0;
    virtual void select(int index) = 0;
    virtual int next(bool repeat) const = 0;
    virtual int previous(bool repeat) const = 0;
};

std::unique_ptr<TrackNavigator> MakeTrackNavigator(std::optional<ShuffleAlgorithm> algorithm);

}  // namespace Core
