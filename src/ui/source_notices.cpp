#include "ui/source_notices.h"

#include "core/player.h"
#include "core/source_notice.h"
#include "ui/player_notices.h"

#include <QCoreApplication>

namespace Ui {

QString SourceNoticeText(const Core::SourceNotice& notice) {
    switch (notice.code) {
        case Core::SourceNoticeCode::Loading:
            return QCoreApplication::translate("Ui::SourceNotices", "%1: loading…").arg(notice.subject);
        case Core::SourceNoticeCode::Failed:
            return QCoreApplication::translate("Ui::SourceNotices", "Error: %1").arg(notice.detail);
        case Core::SourceNoticeCode::VibeFailed: {
            Core::PlayerNotice vibe;
            vibe.code = Core::NoticeCode::VibeFailed;
            vibe.detail = notice.detail;
            return NoticeText(vibe);
        }
        case Core::SourceNoticeCode::Empty:
            return QCoreApplication::translate("Ui::SourceNotices", "%1: empty").arg(notice.subject);
        case Core::SourceNoticeCode::Loaded:
            return QCoreApplication::translate("Ui::SourceNotices", "%1: %n track(s)", nullptr, notice.count)
                .arg(notice.subject);
        case Core::SourceNoticeCode::LoadedFirst:
            return QCoreApplication::translate("Ui::SourceNotices", "%1: the first %n track(s)", nullptr, notice.count)
                .arg(notice.subject);
        case Core::SourceNoticeCode::AddedNext:
            return QCoreApplication::translate("Ui::SourceNotices", "Plays next: %1").arg(notice.subject);
        case Core::SourceNoticeCode::AddedToEnd:
            return QCoreApplication::translate("Ui::SourceNotices", "Added to the end: %1").arg(notice.subject);
        case Core::SourceNoticeCode::AddedToLiked:
            return QCoreApplication::translate("Ui::SourceNotices", "Added to Liked");
        case Core::SourceNoticeCode::RemovedFromLiked:
            return QCoreApplication::translate("Ui::SourceNotices", "Removed from Liked");
        case Core::SourceNoticeCode::Disliked: return QCoreApplication::translate("Ui::SourceNotices", "Disliked");
    }
    return {};
}

}  // namespace Ui
