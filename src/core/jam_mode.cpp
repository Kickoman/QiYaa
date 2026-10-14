#include "core/jam_mode.h"

#include <QPointer>
#include <QtGlobal>

namespace Core {

bool IsJamItem(const QueueEntry* entry) {
    return entry && entry->part == EntryPart::Item && !entry->itemId.isEmpty();
}

JamMode::JamMode(Player* player, QObject* parent)
    : QObject(parent)
    , corePlayer(player) {
    reportTimer.setInterval(kJamPlayingReportMs);
    connect(&reportTimer, &QTimer::timeout, this, [this] {
        if (corePlayer->state() == Player::State::Playing) {
            reportPlayback();
        }
    });
    connect(corePlayer, &Player::trackEvent, this, [this](TrackEvent event) {
        if (!active) {
            return;
        }
        const QueueEntry* entry = corePlayer->currentEntry();
        if (event == TrackEvent::Started && IsJamItem(entry)) {
            Q_EMIT itemStarted(entry->itemId);  // HOST-05
        }
        schedulePlaybackReport();
    });
    connect(corePlayer, &Player::currentTrackChanged, this, &JamMode::schedulePlaybackReport);
    connect(corePlayer, &Player::seeked, this, &JamMode::schedulePlaybackReport);
    connect(corePlayer, &Player::stateChanged, this, &JamMode::schedulePlaybackReport);
}

JamMode::~JamMode() = default;

void JamMode::start(bool waveFeedback) {
    if (active) {
        return;
    }
    active = true;
    QPointer<JamMode> self(this);
    corePlayer->setLayer(QueueLayer{
        // HOST-15, HOST-16: no play reports, no feedback but the jam vibe's; HOST-07: Previous restarts.
        .rules =
            {.playReports = false,
             .previousRestartsOnly = true,
             .ownContinuationFeedback = false,
             .historyFeedback = waveFeedback},
        .removeItems =
            [self](const QStringList& itemIds) {
                if (self) {
                    Q_EMIT self->removeRequested(itemIds);  // HOST-36
                }
            }
    });
    reportTimer.start();
    reportPlayback();
}

void JamMode::setQueue(const QList<JamEntry>& entries, const QStringList& seeds, int seedsVersion) {
    if (!active) {
        return;
    }
    const bool idle = nothingToPlay();
    corePlayer->setLayerItems(entries, SeedHint{.seeds = seeds, .version = seedsVersion});
    if (idle && corePlayer->hasNext()) {
        corePlayer->next();  // HOST-08: nothing plays, the first item starts at once
    }
}

void JamMode::skip(const QString& itemId) {
    const QueueEntry* entry = corePlayer->currentEntry();
    if (active && IsJamItem(entry) && entry->itemId == itemId) {
        corePlayer->next();  // HOST-18; any other item: HOST-19
    }
}

void JamMode::end() {
    if (!active) {
        return;
    }
    reportTimer.stop();
    active = false;
    corePlayer->setLayer(std::nullopt);  // HOST-32, HOST-33
}

void JamMode::reportPlayback() {
    if (!active) {
        return;
    }
    JamPlayback report;
    const Track* track = corePlayer->currentTrack();
    const Player::State state = corePlayer->state();
    if (track && !nothingToPlay()) {
        const QueueEntry* entry = corePlayer->currentEntry();
        const bool item = IsJamItem(entry);
        report.kind = item ? JamPlayback::Kind::Item : JamPlayback::Kind::Base;
        report.itemId = item ? entry->itemId : QString();
        report.track = *track;
        report.link = corePlayer->currentLink();
        report.nextLink = corePlayer->nextLink();
        const bool stopped = state == Player::State::Stopped;
        report.positionMs = stopped ? 0 : static_cast<qint64>(corePlayer->positionSeconds() * 1000);
        report.paused = state != Player::State::Playing;
    }
    Q_EMIT playback(report);
}

bool JamMode::nothingToPlay() const {
    switch (corePlayer->state()) {
        case Player::State::Empty:
        case Player::State::WaitingForMore: return true;
        case Player::State::Stopped: return !corePlayer->hasNext();
        case Player::State::Loading:
        case Player::State::Playing:
        case Player::State::Paused:
        case Player::State::WaitingForNetwork: return false;
    }
    return false;
}

void JamMode::schedulePlaybackReport() {
    if (!active || reportPending) {
        return;
    }
    reportPending = true;
    QTimer::singleShot(0, this, [this] {
        reportPending = false;
        reportPlayback();
    });
}

}  // namespace Core
