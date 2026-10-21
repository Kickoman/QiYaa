#pragma once

#include "core/lyrics.h"
#include "core/track.h"

#include <QList>
#include <QObject>
#include <QTimer>

#include <optional>

namespace Core {

class Player;
class SourceRegistry;

class LyricsService : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    virtual void lyrics(const Track& track, TLyricsDone done) = 0;
    virtual void cancel() = 0;
};

struct LyricsLookup {
    enum class Shown { SourceSynced, ThirdPartySynced, SourceText, ThirdPartyText, Instrumental, None };

    Shown shown = Shown::None;
    bool thirdPartyFailed = false;
};

class LyricsFinder : public QObject {
    Q_OBJECT
public:
    enum class Phase { Idle, Loading, Shown, Missing, Instrumental, Failed, NoAccount };

    struct View {
        std::optional<Track> track;
        Phase phase = Phase::Idle;
        Lyrics lyrics;
        LyricsFrom from = LyricsFrom::Catalog;
        bool better = false;
    };

    LyricsFinder(Player* player, SourceRegistry* sources, LyricsService* thirdParty, QObject* parent = nullptr);

    void setActive(bool on);
    bool isActive() const { return active; }
    void setThirdPartyEnabled(bool on);
    bool thirdPartyEnabled() const { return thirdPartyOn; }
    bool hasThirdParty() const { return service != nullptr; }
    void retryFailed();
    void forget();
    void setWaits(int answerMs, int sourceSilentMs);

    const View& view() const { return shown; }

Q_SIGNALS:
    void changed();
    void lookedUp(const Core::LyricsLookup& lookup);

private:
    struct Remembered {
        TrackKey key;
        std::optional<LyricsAnswer> source;
        std::optional<LyricsAnswer> thirdParty;
    };

    enum class Rank { SourceSynced, ThirdPartySynced, SourceText, ThirdPartyText, Nothing };

    void follow();
    void start(const Track& track);
    void askSource();
    void maybeAskThirdParty();
    void sourceAnswered(quint64 request, LyricsAnswer answer);
    void thirdPartyAnswered(quint64 request, LyricsAnswer answer);
    void sourceTimedOut();
    void thirdPartyTimedOut();
    void cancelThirdParty();
    void remember(const LyricsAnswer& answer, bool fromSource);
    const Remembered* rememberedFor(const TrackKey& key) const;
    Rank rank() const;
    void update();
    void maybeFinish();
    void finishLookup();
    void drop();
    void stop();

    Player* player;
    SourceRegistry* sources;
    LyricsService* service;
    bool active = false;
    bool thirdPartyOn = false;
    int answerWaitMs = 8000;
    int sourceSilentWaitMs = 1000;

    std::optional<Track> track;
    quint64 sourceRequest = 0;
    quint64 thirdPartyRequest = 0;
    bool sourcePending = false;
    bool thirdPartyPending = false;
    bool sourceSilent = false;
    std::optional<LyricsAnswer> sourceAnswer;
    std::optional<LyricsAnswer> thirdPartyAnswer;
    bool lookupOpen = false;
    bool lookupAsked = false;

    QTimer sourceWait;
    QTimer sourceSilence;
    QTimer thirdPartyWait;
    QList<Remembered> memory;
    View shown;
};

}  // namespace Core
