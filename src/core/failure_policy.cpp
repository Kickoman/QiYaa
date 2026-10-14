#include "core/failure_policy.h"

namespace Core {

FailureAction DecideOnFailure(FailureKind kind, int failuresInRow, bool hasNext, bool endless) {
    switch (kind) {
        case FailureKind::Network: return FailureAction::WaitForNetwork;
        case FailureKind::Auth: return FailureAction::Stop;
        case FailureKind::Track: break;
    }
    if (failuresInRow + 1 >= kMaxTrackFailuresInRow) {
        return FailureAction::StopAfterLimit;
    }
    return hasNext || endless ? FailureAction::Next : FailureAction::Stop;
}

}  // namespace Core
