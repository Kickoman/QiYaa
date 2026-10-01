#pragma once

#include "yandex/api_client.h"

namespace Core {

// Why a track cannot play (spec/player/errors.md): the network, the account (HTTP 401/403), or
// the track itself (any other HTTP error, no usable download variant, audio that cannot be
// decoded).
enum class FailureKind { Network, Auth, Track };

// What the Player does about it.
enum class FailureAction {
    WaitForNetwork,  // ERR-01 to ERR-03: pause on the track, try again later
    Next,  // ERR-04: the next track
    Stop,  // ERR-07 (no next track), ERR-08 (the account)
    StopAfterLimit,  // ERR-05: too many broken tracks in a row
};

inline constexpr int kMaxTrackFailuresInRow = 3;

FailureKind KindOf(const Yandex::RequestError& error);

// `failuresInRow` counts the track failures before this one. `hasNext`: a track follows in a
// finite queue; `endless`: the queue is a wave, which waits for more instead of ending.
FailureAction DecideOnFailure(FailureKind kind, int failuresInRow, bool hasNext, bool endless);

}  // namespace Core
