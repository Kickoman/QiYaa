#pragma once

#include <QString>

namespace Core {

enum class SourceNoticeCode {
    Loading,
    Failed,
    VibeFailed,
    Empty,
    Loaded,
    LoadedFirst,
    AddedNext,
    AddedToEnd,
    AddedToLiked,
    RemovedFromLiked,
    Disliked,
};

struct SourceNotice {
    SourceNoticeCode code = SourceNoticeCode::Failed;
    QString subject{};
    QString detail{};
    int count = 0;
};

}  // namespace Core
