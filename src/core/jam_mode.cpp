#include "core/jam_mode.h"

#include "audio/audio_engine.h"

#include <QPointer>
#include <QtGlobal>

#include <algorithm>
#include <functional>
#include <utility>

namespace Core {

using Yandex::Track;

namespace {

QList<Track> Playable(const QList<Track>& tracks) {
    QList<Track> playable;
    std::copy_if(tracks.cbegin(), tracks.cend(), std::back_inserter(playable), [](const Track& track) {
        return track.available;
    });
    return playable;
}

}  // namespace

JamMode::JamMode(Player* player, TJamWaveFactory makeWave, QObject* parent)
    : QObject(parent)
    , corePlayer(player)
    , waveFactory(std::move(makeWave)) {
    reportTimer.setInterval(kJamPlayingReportMs);
    connect(&reportTimer, &QTimer::timeout, this, [this] {
        if (corePlayer->engine()->state() == Audio::AudioEngine::State::Playing) {
            reportPlayback();
        }
    });
    connect(corePlayer, &Player::currentTrackChanged, this, [this] {
        if (!active) {
            return;
        }
        schedulePlaybackReport();
        QTimer::singleShot(0, this, &JamMode::dropDeferredWave);
    });
    connect(corePlayer, &Player::seeked, this, &JamMode::schedulePlaybackReport);
    connect(corePlayer->engine(), &Audio::AudioEngine::stateChanged, this, &JamMode::schedulePlaybackReport);
    connect(corePlayer, &Player::playlistChanged, this, &JamMode::checkSlots);
}

JamMode::~JamMode() = default;

JamSlot JamMode::currentSlot() const {
    const int cursor = corePlayer->currentIndex();
    return cursor >= 0 && cursor < trackSlots.size() ? trackSlots[cursor] : JamSlot{};
}

void JamMode::start(const QString& title, bool waveFeedback) {
    if (active) {
        return;
    }
    QPointer<JamMode> self(this);
    wave = waveFactory(waveFeedback, [self] { return self ? self->recentWaveTracks() : QList<Track>{}; });
    lastSeeds.clear();
    lastSeedsVersion = -1;

    const int cursor = corePlayer->currentIndex();
    QList<int> others;
    for (int i = 0; i < corePlayer->playlist().size(); ++i) {
        if (i != cursor) {
            others << i;
        }
    }
    if (!others.isEmpty()) {
        corePlayer->removeTracks(others);  // HOST-14: the playing track stays, the rest goes
    }
    trackSlots = corePlayer->playlist().isEmpty() ? QList<JamSlot>{} : QList<JamSlot>{JamSlot{}};
    active = true;

    const std::shared_ptr<JamWave> state = wave;
    Player::TLoadMoreCallback more = [self](std::function<void(const QList<Track>&)> done) {
        if (!self || !self->active) {
            return done({});
        }
        self->loadWave(std::move(done));
    };
    Player::TEventCallback events = [self, state](Player::TrackEvent event, const Track& track, double played) {
        if (self && self->active) {
            const JamSlot slot = self->currentSlot();
            if (event == Player::TrackEvent::Started && slot.kind == JamSlot::Kind::Item) {
                Q_EMIT self->itemStarted(slot.itemId);  // HOST-05
            }
            self->schedulePlaybackReport();
        }
        if (!self || !self->active || self->currentSlot().kind == JamSlot::Kind::Wave) {
            state->trackEvent(event, track, played);  // HOST-16
        }
    };
    // HOST-15: no play reports; HOST-07: Previous restarts the track.
    corePlayer->changeSource(title, std::move(more), std::move(events), QueueRules{false, true});
    reportTimer.start();
    reportPlayback();
}

void JamMode::setQueue(const QList<JamEntry>& entries, const QStringList& seeds, int seedsVersion) {
    if (!active) {
        return;
    }
    lastSeeds = seeds;
    lastSeedsVersion = seedsVersion;
    if (corePlayer->playlist().isEmpty()) {
        if (!entries.isEmpty()) {  // HOST-08: nothing plays, the first item starts at once
            insertItems(0, entries);
            corePlayer->playIndex(0);
        }
        return;
    }
    const int cursor = corePlayer->currentIndex();
    const JamSlot current = currentSlot();
    const bool tailWasEmpty = cursor + 1 >= corePlayer->playlist().size();
    QList<JamEntry> wanted;
    for (const JamEntry& entry : entries) {
        if (current.kind != JamSlot::Kind::Item || entry.itemId != current.itemId) {
            wanted << entry;  // HOST-04: the current item never goes back into the tail
        }
    }
    int end = cursor + 1;
    while (end < trackSlots.size() && trackSlots[end].kind == JamSlot::Kind::Item) {
        ++end;
    }
    int prefix = 0;
    while (cursor + 1 + prefix < end && prefix < wanted.size()
           && trackSlots[cursor + 1 + prefix].itemId == wanted[prefix].itemId) {
        ++prefix;  // HOST-01, HOST-02: the common prefix stays, with its preload
    }
    QList<int> changed;
    for (int i = cursor + 1 + prefix; i < end; ++i) {
        changed << i;
    }
    if (!changed.isEmpty()) {
        removeSlots(changed);
    }
    const QList<JamEntry> added = wanted.mid(prefix);
    if (!added.isEmpty()) {
        insertItems(cursor + 1 + prefix, added);
        if (corePlayer->isWaitingForMore() || (tailWasEmpty && !corePlayer->trackInProgress())) {
            corePlayer->playIndex(cursor + 1);  // HOST-08: nothing plays, the item starts at once
        }
    }
    dropDeferredWave();
    corePlayer->requestMore();
}

void JamMode::skip(const QString& itemId) {
    const JamSlot slot = currentSlot();
    if (active && slot.kind == JamSlot::Kind::Item && slot.itemId == itemId) {
        corePlayer->next();  // HOST-18; any other item: HOST-19
    }
}

void JamMode::end() {
    if (!active) {
        return;
    }
    reportTimer.stop();
    const int cursor = corePlayer->currentIndex();
    QList<int> waveTracks;
    for (int i = cursor + 1; i < trackSlots.size(); ++i) {
        if (trackSlots[i].kind == JamSlot::Kind::Wave) {
            waveTracks << i;
        }
    }
    active = false;
    trackSlots.clear();
    if (!waveTracks.isEmpty()) {
        corePlayer->removeTracks(waveTracks);  // HOST-32: the jam items stay as ordinary tracks
    }
    corePlayer->changeSource(corePlayer->queueTitle(), {}, {}, QueueRules{});  // HOST-33
}

void JamMode::reportPlayback() {
    if (!active) {
        return;
    }
    JamPlayback report;
    const Track* track = corePlayer->currentTrack();
    const auto state = corePlayer->engine()->state();
    const bool stopped = state == Audio::AudioEngine::State::Stopped;
    if (track && !(stopped && corePlayer->isWaitingForMore())) {
        const JamSlot slot = currentSlot();
        report.kind = slot.kind == JamSlot::Kind::Item ? JamPlayback::Kind::Item : JamPlayback::Kind::Wave;
        report.itemId = slot.kind == JamSlot::Kind::Item ? slot.itemId : QString();
        report.track = *track;
        report.link = corePlayer->currentLink();
        report.nextLink = corePlayer->nextLink();
        report.positionMs = stopped ? 0 : static_cast<qint64>(corePlayer->engine()->positionSeconds() * 1000);
        report.paused = state != Audio::AudioEngine::State::Playing;
    }
    Q_EMIT playback(report);
}

void JamMode::loadWave(JamWave::TDone done) {
    QPointer<JamMode> self(this);
    auto deliver = [self, done](const QList<Track>& tracks) {
        if (!self || !self->active) {
            return done({});
        }
        self->appendWave(tracks, done);
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
    for (qsizetype i = trackSlots.size() - 1; i >= 0 && recent.size() < kJamWaveHistory; --i) {
        if (trackSlots[i].kind == JamSlot::Kind::Wave && i < corePlayer->playlist().size()) {
            recent.prepend(corePlayer->playlist()[i]);
        }
    }
    return recent;
}

void JamMode::appendWave(const QList<Track>& tracks, JamWave::TDone done) {
    const QList<Track> playable = Playable(tracks);
    for (qsizetype i = 0; i < playable.size(); ++i) {
        trackSlots << JamSlot{JamSlot::Kind::Wave, {}, {}};
    }
    done(playable);
    checkSlots();
}

void JamMode::dropDeferredWave() {
    if (!active || !wave->hasSession() || wave->seedsVersion() == lastSeedsVersion
        || currentSlot().kind == JamSlot::Kind::Wave) {
        return;
    }
    const int cursor = corePlayer->currentIndex();
    QList<int> deferred;
    for (int i = cursor + 1; i < trackSlots.size(); ++i) {
        if (trackSlots[i].kind == JamSlot::Kind::Item) {
            return;  // the jam part is not empty yet
        }
        if (trackSlots[i].kind == JamSlot::Kind::Wave) {
            deferred << i;
        }
    }
    wave->endSession();  // HOST-11
    if (!deferred.isEmpty()) {
        removeSlots(deferred);
    }
    corePlayer->requestMore();
}

void JamMode::removeSlots(const QList<int>& indices) {
    QList<int> sorted = indices;
    std::sort(sorted.begin(), sorted.end(), std::greater<>());
    for (int index : sorted) {
        trackSlots.removeAt(index);
    }
    corePlayer->removeTracks(indices);
}

void JamMode::insertItems(int at, const QList<JamEntry>& entries) {
    QList<Track> tracks;
    for (qsizetype i = 0; i < entries.size(); ++i) {
        trackSlots.insert(at + i, JamSlot{JamSlot::Kind::Item, entries[i].itemId, entries[i].addedBy});
        Track track = entries[i].track;
        track.available = true;
        tracks << track;
    }
    corePlayer->insertTracks(at, tracks);
}

void JamMode::checkSlots() {
    if (!active || trackSlots.size() == corePlayer->playlist().size()) {
        return;
    }
    qWarning(
        "jam: the queue has %lld tracks but the jam knows %lld; the queue was changed outside it",
        static_cast<long long>(corePlayer->playlist().size()),
        static_cast<long long>(trackSlots.size())
    );
    trackSlots.resize(corePlayer->playlist().size());
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
