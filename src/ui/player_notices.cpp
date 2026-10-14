#include "ui/player_notices.h"

#include "core/player.h"

#include <QCoreApplication>

namespace Ui {

QString NoticeText(const Core::PlayerNotice& notice) {
    switch (notice.code) {
        case Core::NoticeCode::ShuffleOffInVibe:
            return QCoreApplication::translate("Ui::PlayerNotices", "Shuffle does not apply to a vibe");
        case Core::NoticeCode::AudioError:
            return QCoreApplication::translate("Ui::PlayerNotices", "Audio error: %1").arg(notice.detail);
        case Core::NoticeCode::NoOutputDevice:
            return QCoreApplication::translate("Ui::PlayerNotices", "No audio output device");
        case Core::NoticeCode::LoadingMore:
            return QCoreApplication::translate("Ui::PlayerNotices", "Loading more tracks…");
        case Core::NoticeCode::DownloadFailed:
            return QCoreApplication::translate("Ui::PlayerNotices", "Download failed: %1").arg(notice.detail);
        case Core::NoticeCode::TrackFailed: {
            const QString reason = notice.undecodable
                ? QCoreApplication::translate("Ui::PlayerNotices", "the sound could not be decoded")
                : notice.detail;
            return QCoreApplication::translate("Ui::PlayerNotices", "The track does not play: %1").arg(reason);
        }
        case Core::NoticeCode::AccessDenied:
            return QCoreApplication::translate("Ui::PlayerNotices", "Access error: %1").arg(notice.detail);
        case Core::NoticeCode::StoppedAfterFailures:
            return QCoreApplication::translate(
                "Ui::PlayerNotices", "Stopped: %n track(s) in a row did not play", nullptr, notice.count
            );
        case Core::NoticeCode::WaitingForNetwork:
            return QCoreApplication::translate("Ui::PlayerNotices", "No network — waiting for it…");
    }
    return {};
}

}  // namespace Ui
