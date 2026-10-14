#include "core/jam_mode.h"

#include <QPointer>
#include <QtGlobal>

#include <functional>
#include <utility>

namespace Core {

namespace {

class JamWaveContinuation : public Continuation {
public:
    using TLoad = std::function<void(TDone done)>;

    JamWaveContinuation(TLoad loader, std::shared_ptr<JamWave> jamWave)
        : load(std::move(loader))
        , wave(std::move(jamWave)) { }

    bool more(const ContinuationRequest&, TDone done) override {
        load(std::move(done));
        return true;
    }
    void trackEvent(TrackEvent event, const Track& track, double playedSeconds) override {
        wave->trackEvent(event, track, playedSeconds);  // HOST-16
    }

private:
    TLoad load;
    std::shared_ptr<JamWave> wave;
};

bool IsJamItem(const QueueEntry* entry) {
    return entry && entry->part == EntryPart::Item && !entry->itemId.isEmpty();
}

}  // namespace

JamMode::JamMode(Player* player, TJamWaveFactory makeWave, QObject* parent)
    : QObject(parent)
    , corePlayer(player)
    , waveFactory(std::move(makeWave)) {
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

JamSlot JamMode::slotAt(int row) const {
    const QList<QueueEntry>& entries = corePlayer->entries();
    if (!active || row < 0 || row >= entries.size()) {
        return {};
    }
    const QueueEntry& entry = entries[row];
    if (IsJamItem(&entry)) {
        return JamSlot{JamSlot::Kind::Item, entry.itemId, entry.addedBy};
    }
    if (entry.part == EntryPart::Continuation && entry.giver == waveContinuation) {
        return JamSlot{JamSlot::Kind::Wave, {}, {}};
    }
    return {};
}

QList<JamSlot> JamMode::queueSlots() const {
    QList<JamSlot> slots;
    for (int row = 0; active && row < corePlayer->entries().size(); ++row) {
        slots << slotAt(row);
    }
    return slots;
}

void JamMode::start(const QString& title, bool waveFeedback) {
    if (active) {
        return;
    }
    QPointer<JamMode> self(this);
    wave = waveFactory(waveFeedback, [self] { return self ? self->recentWaveTracks() : QList<Track>{}; });
    lastSeeds.clear();
    lastSeedsVersion = -1;
    const auto load = [self](Continuation::TDone done) {
        if (!self) {
            return done({});
        }
        self->loadWave(std::move(done));
    };
    waveContinuation = std::make_shared<JamWaveContinuation>(load, wave);
    active = true;
    // HOST-15: no play reports; HOST-07: Previous restarts the track.
    corePlayer->setLayer(
        QueueLayer{.rules = {.playReports = false, .previousRestartsOnly = true}, .wave = waveContinuation}
    );
    corePlayer->setSource(QueueSource{.title = title}, false);  // HOST-14: the playing track stays, the rest goes
    reportTimer.start();
    reportPlayback();
}

void JamMode::setQueue(const QList<JamEntry>& entries, const QStringList& seeds, int seedsVersion) {
    if (!active) {
        return;
    }
    lastSeeds = seeds;
    lastSeedsVersion = seedsVersion;
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
    corePlayer->setLayer(std::nullopt);  // HOST-32: the jam items stay as ordinary tracks; HOST-33
    waveContinuation.reset();
}

void JamMode::reportPlayback() {
    if (!active) {
        return;
    }
    JamPlayback report;
    const Track* track = corePlayer->currentTrack();
    const Player::State state = corePlayer->state();
    if (track && state != Player::State::WaitingForMore) {
        const QueueEntry* entry = corePlayer->currentEntry();
        const bool item = IsJamItem(entry);
        report.kind = item ? JamPlayback::Kind::Item : JamPlayback::Kind::Wave;
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

void JamMode::loadWave(Continuation::TDone done) {
    const auto deliver = [done, version = lastSeedsVersion](const QList<Track>& tracks) {
        done(ContinuationReply{.tracks = tracks, .seedsVersion = version});
    };
    if (wave->hasSession() && wave->seedsVersion() == lastSeedsVersion) {  // HOST-11
        return wave->more(deliver);
    }
    if (!wave->start(lastSeeds, lastSeedsVersion, corePlayer->currentTrack(), deliver)) {  // HOST-13
        done({});
        return reportPlayback();
    }
}

QList<Track> JamMode::recentWaveTracks() const {
    QList<Track> recent;
    const QList<QueueEntry>& entries = corePlayer->entries();
    for (qsizetype i = entries.size() - 1; i >= 0 && recent.size() < kJamWaveHistory; --i) {
        if (waveContinuation && entries[i].giver == waveContinuation) {
            recent.prepend(entries[i].track);
        }
    }
    return recent;
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
