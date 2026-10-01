#include "core/failure_policy.h"

namespace Core {

FailureKind KindOf(const Yandex::RequestError& error) {
    switch (error.kind) {
        case Yandex::RequestError::Kind::Network: return FailureKind::Network;
        case Yandex::RequestError::Kind::Http:
            return error.httpStatus == 401 || error.httpStatus == 403 ? FailureKind::Auth
                                                                      : FailureKind::Track;
        case Yandex::RequestError::Kind::None:
        case Yandex::RequestError::Kind::Content: break;
    }
    return FailureKind::Track;
}

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
