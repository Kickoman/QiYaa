#include "core/lyrics_finder.h"

#include "core/player.h"
#include "core/track_source.h"

#include <QPointer>

#include <algorithm>
#include <utility>

namespace Core {

namespace {

constexpr qsizetype kRememberedTracks = 10;

bool HasText(const std::optional<LyricsAnswer>& answer, LyricsKind kind) {
    return answer && answer->status == LyricsAnswer::Status::Found && answer->lyrics.kind == kind;
}

bool Is(const std::optional<LyricsAnswer>& answer, LyricsAnswer::Status status) {
    return answer && answer->status == status;
}

}  // namespace

LyricsFinder::LyricsFinder(Player* corePlayer, SourceRegistry* registry, LyricsService* thirdParty, QObject* parent)
    : QObject(parent)
    , player(corePlayer)
    , sources(registry)
    , service(thirdParty) {
    sourceWait.setSingleShot(true);
    sourceSilence.setSingleShot(true);
    thirdPartyWait.setSingleShot(true);
    connect(&sourceWait, &QTimer::timeout, this, &LyricsFinder::sourceTimedOut);
    connect(&sourceSilence, &QTimer::timeout, this, [this] {
        sourceSilent = true;
        maybeAskThirdParty();
        update();
    });
    connect(&thirdPartyWait, &QTimer::timeout, this, &LyricsFinder::thirdPartyTimedOut);
    connect(player, &Player::currentTrackChanged, this, &LyricsFinder::follow);
    connect(player, &Player::playlistChanged, this, &LyricsFinder::follow);
}

void LyricsFinder::setActive(bool on) {
    if (on == active) {
        return;
    }
    active = on;
    if (on) {
        follow();
    } else {
        stop();
    }
}

void LyricsFinder::setThirdPartyEnabled(bool on) {
    if (on == thirdPartyOn) {
        return;
    }
    thirdPartyOn = on;
    if (!on) {
        cancelThirdParty();
        thirdPartyAnswer.reset();
    } else if (active && track && !lookupOpen) {
        lookupOpen = true;
        lookupAsked = false;
    }
    maybeAskThirdParty();
    update();
    maybeFinish();
}

void LyricsFinder::retryFailed() {
    if (!active || !track) {
        return;
    }
    const bool sourceFailed = !sourcePending && Is(sourceAnswer, LyricsAnswer::Status::Failed);
    const bool thirdPartyFailed = Is(thirdPartyAnswer, LyricsAnswer::Status::Failed);
    if (!sourceFailed && !thirdPartyFailed) {
        return;
    }
    if (!lookupOpen) {
        lookupOpen = true;
        lookupAsked = false;
    }
    if (thirdPartyFailed) {
        thirdPartyAnswer.reset();
    }
    if (sourceFailed) {
        sourceAnswer.reset();
        askSource();
    }
    maybeAskThirdParty();
    update();
    maybeFinish();
}

void LyricsFinder::forget() {
    memory.clear();
    if (active && track) {
        const Track again = *track;
        finishLookup();
        start(again);
    }
}

void LyricsFinder::setWaits(int answerMs, int sourceSilentMs) {
    answerWaitMs = answerMs;
    sourceSilentWaitMs = sourceSilentMs;
}

void LyricsFinder::follow() {
    if (!active) {
        return;
    }
    const Track* current = player->currentTrack();
    if (!current) {
        stop();
        return;
    }
    if (track && track->key == current->key) {
        return;
    }
    finishLookup();
    start(*current);
}

void LyricsFinder::start(const Track& next) {
    drop();
    track = next;
    lookupOpen = true;
    lookupAsked = false;
    const Remembered* kept = rememberedFor(next.key);
    if (kept && kept->source) {
        sourceAnswer = kept->source;
    } else {
        askSource();
    }
    maybeAskThirdParty();
    update();
    maybeFinish();
}

void LyricsFinder::askSource() {
    sourcePending = true;
    sourceSilent = false;
    lookupAsked = true;
    const quint64 request = ++sourceRequest;
    sourceWait.start(answerWaitMs);
    sourceSilence.start(sourceSilentWaitMs);
    sources->lyrics(*track, [self = QPointer<LyricsFinder>(this), request](LyricsAnswer answer) {
        if (self) {
            self->sourceAnswered(request, std::move(answer));
        }
    });
}

void LyricsFinder::maybeAskThirdParty() {
    if (!active || !track || !service || !thirdPartyOn || thirdPartyPending || thirdPartyAnswer) {
        return;
    }
    if (HasText(sourceAnswer, LyricsKind::Synced) || (!sourceAnswer && !sourceSilent)) {
        return;
    }
    if (const Remembered* kept = rememberedFor(track->key); kept && kept->thirdParty) {
        thirdPartyAnswer = kept->thirdParty;
        thirdPartyAnswer->serverError = false;
        return;
    }
    thirdPartyPending = true;
    lookupAsked = true;
    const quint64 request = ++thirdPartyRequest;
    thirdPartyWait.start(answerWaitMs);
    service->lyrics(*track, [self = QPointer<LyricsFinder>(this), request](LyricsAnswer answer) {
        if (self) {
            self->thirdPartyAnswered(request, std::move(answer));
        }
    });
}

void LyricsFinder::sourceAnswered(quint64 request, LyricsAnswer answer) {
    if (request != sourceRequest || !sourcePending) {
        return;
    }
    sourcePending = false;
    sourceWait.stop();
    sourceSilence.stop();
    remember(answer, true);
    sourceAnswer = std::move(answer);
    if (HasText(sourceAnswer, LyricsKind::Synced)) {
        cancelThirdParty();
    }
    maybeAskThirdParty();
    update();
    maybeFinish();
}

void LyricsFinder::thirdPartyAnswered(quint64 request, LyricsAnswer answer) {
    if (request != thirdPartyRequest || !thirdPartyPending) {
        return;
    }
    thirdPartyPending = false;
    thirdPartyWait.stop();
    answer.from = LyricsFrom::ThirdParty;
    remember(answer, false);
    thirdPartyAnswer = std::move(answer);
    update();
    maybeFinish();
}

void LyricsFinder::sourceTimedOut() {
    if (!sourcePending) {
        return;
    }
    ++sourceRequest;
    sourcePending = false;
    sourceSilence.stop();
    sourceAnswer = LyricsAnswer{};
    maybeAskThirdParty();
    update();
    maybeFinish();
}

void LyricsFinder::thirdPartyTimedOut() {
    if (!thirdPartyPending) {
        return;
    }
    cancelThirdParty();
    thirdPartyAnswer = LyricsAnswer{.from = LyricsFrom::ThirdParty};
    update();
    maybeFinish();
}

void LyricsFinder::cancelThirdParty() {
    if (!thirdPartyPending) {
        return;
    }
    thirdPartyPending = false;
    ++thirdPartyRequest;
    thirdPartyWait.stop();
    service->cancel();
}

void LyricsFinder::remember(const LyricsAnswer& answer, bool fromSource) {
    if (!track || answer.status == LyricsAnswer::Status::Failed || answer.status == LyricsAnswer::Status::NoAccount) {
        return;
    }
    Remembered entry{track->key, {}, {}};
    const auto found = std::find_if(memory.begin(), memory.end(), [this](const Remembered& candidate) {
        return candidate.key == track->key;
    });
    if (found != memory.end()) {
        entry = std::move(*found);
        memory.erase(found);
    }
    (fromSource ? entry.source : entry.thirdParty) = answer;
    memory.prepend(std::move(entry));
    memory.resize(std::min(memory.size(), kRememberedTracks));
}

const LyricsFinder::Remembered* LyricsFinder::rememberedFor(const TrackKey& key) const {
    const auto found = std::find_if(memory.cbegin(), memory.cend(), [&key](const Remembered& candidate) {
        return candidate.key == key;
    });
    return found != memory.cend() ? &*found : nullptr;
}

LyricsFinder::Rank LyricsFinder::rank() const {
    if (HasText(sourceAnswer, LyricsKind::Synced)) {
        return Rank::SourceSynced;
    }
    if (HasText(thirdPartyAnswer, LyricsKind::Synced)) {
        return Rank::ThirdPartySynced;
    }
    if (HasText(sourceAnswer, LyricsKind::Plain)) {
        return Rank::SourceText;
    }
    if (HasText(thirdPartyAnswer, LyricsKind::Plain)) {
        return Rank::ThirdPartyText;
    }
    return Rank::Nothing;
}

void LyricsFinder::update() {
    View next;
    next.track = track;
    const Rank best = rank();
    if (!active || !track) {
        next.phase = Phase::Idle;
    } else if (best != Rank::Nothing) {
        const bool fromSource = best == Rank::SourceSynced || best == Rank::SourceText;
        const LyricsAnswer& answer = fromSource ? *sourceAnswer : *thirdPartyAnswer;
        next.phase = Phase::Shown;
        next.lyrics = answer.lyrics;
        next.from = answer.from;
        next.better = (sourcePending && best != Rank::SourceSynced)
            || (thirdPartyPending && (best == Rank::SourceText || best == Rank::ThirdPartyText));
    } else if (Is(thirdPartyAnswer, LyricsAnswer::Status::Instrumental)) {
        next.phase = Phase::Instrumental;
    } else if (sourcePending || thirdPartyPending) {
        next.phase = Phase::Loading;
    } else if (Is(sourceAnswer, LyricsAnswer::Status::NoAccount)) {
        next.phase = Phase::NoAccount;
    } else if (Is(sourceAnswer, LyricsAnswer::Status::Failed)) {
        next.phase = Phase::Failed;
    } else {
        next.phase = Phase::Missing;
    }
    shown = std::move(next);
    Q_EMIT changed();
}

void LyricsFinder::maybeFinish() {
    if (lookupOpen && !sourcePending && !thirdPartyPending) {
        finishLookup();
    }
}

void LyricsFinder::finishLookup() {
    if (!lookupOpen) {
        return;
    }
    lookupOpen = false;
    if (!lookupAsked) {
        return;
    }
    LyricsLookup lookup;
    switch (rank()) {
        case Rank::SourceSynced: lookup.shown = LyricsLookup::Shown::SourceSynced; break;
        case Rank::ThirdPartySynced: lookup.shown = LyricsLookup::Shown::ThirdPartySynced; break;
        case Rank::SourceText: lookup.shown = LyricsLookup::Shown::SourceText; break;
        case Rank::ThirdPartyText: lookup.shown = LyricsLookup::Shown::ThirdPartyText; break;
        case Rank::Nothing:
            lookup.shown = Is(thirdPartyAnswer, LyricsAnswer::Status::Instrumental)
                ? LyricsLookup::Shown::Instrumental
                : LyricsLookup::Shown::None;
            break;
    }
    lookup.thirdPartyFailed =
        Is(thirdPartyAnswer, LyricsAnswer::Status::Failed) || (thirdPartyAnswer && thirdPartyAnswer->serverError);
    Q_EMIT lookedUp(lookup);
}

void LyricsFinder::drop() {
    ++sourceRequest;
    sourcePending = false;
    sourceSilent = false;
    sourceWait.stop();
    sourceSilence.stop();
    cancelThirdParty();
    sourceAnswer.reset();
    thirdPartyAnswer.reset();
    track.reset();
}

void LyricsFinder::stop() {
    finishLookup();
    drop();
    update();
}

}  // namespace Core
