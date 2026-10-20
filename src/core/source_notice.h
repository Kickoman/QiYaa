#pragma once

#include <QString>

namespace Core {

enum class SourceNoticeCode {
    Loading,
    Searching,
    Failed,
    VibeFailed,
    SearchFailed,
    Empty,
    NothingFound,
    Loaded,
    LoadedFirst,
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
