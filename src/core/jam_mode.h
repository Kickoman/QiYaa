#pragma once

#include "core/continuation.h"
#include "core/player.h"
#include "core/track.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>

namespace Core {

struct JamEntry {
    QString itemId;
    Track track;
    QString addedBy;
};

struct JamSlot {
    enum class Kind { Item, Wave, Other };
    Kind kind = Kind::Other;
    QString itemId;
    QString addedBy;

    bool operator==(const JamSlot&) const = default;
};

struct JamPlayback {
    enum class Kind { Item, Wave, Idle };
    Kind kind = Kind::Idle;
    QString itemId;
    std::optional<Track> track;
    qint64 positionMs = 0;
    bool paused = true;
    QUrl link;  // the current file, for guests who listen along (spec/jam/listen.md)
    QUrl nextLink;  // the preloaded next file
};

inline constexpr int kJamPlayingReportMs = 10'000;
inline constexpr int kJamWaveHistory = 5;

class JamWave : public Continuation {
public:
    using TRecent = std::function<QList<Track>()>;

    virtual bool start(const QStringList& seeds, int seedsVersion, const Track* current, TDone done) = 0;
    virtual bool hasSession() const = 0;
    virtual int seedsVersion() const = 0;
    virtual void endSession() = 0;
};

using TJamWaveFactory = std::function<std::shared_ptr<JamWave>(bool feedback, JamWave::TRecent recent)>;

// The jam mode of the queue (spec/jam/host.md, HOST-01 to HOST-17, HOST-32, HOST-33): the
// current track, then the jam part (the mirror of the server's queue), then the jam wave.
class JamMode : public QObject {
    Q_OBJECT
public:
    JamMode(Player* player, TJamWaveFactory makeWave, QObject* parent = nullptr);
    ~JamMode() override;

    bool isActive() const { return active; }
    const QList<JamSlot>& queueSlots() const { return trackSlots; }
    JamSlot currentSlot() const;

    void start(const QString& title, bool waveFeedback);
    void setQueue(const QList<JamEntry>& entries, const QStringList& seeds, int seedsVersion);
    void skip(const QString& itemId);
    void end();
    void reportPlayback();
    void setPlayingReportInterval(int ms) { reportTimer.setInterval(ms); }

Q_SIGNALS:
    void itemStarted(const QString& itemId);
    void playback(const Core::JamPlayback& playback);

private:
    void loadWave(JamWave::TDone done);
    void appendWave(const QList<Track>& tracks, JamWave::TDone done);
    QList<Track> recentWaveTracks() const;
    void dropDeferredWave();
    void removeSlots(const QList<int>& indices);
    void insertItems(int at, const QList<JamEntry>& entries);
    void checkSlots();
    void schedulePlaybackReport();

    Player* corePlayer;
    TJamWaveFactory waveFactory;
    bool active = false;
    QList<JamSlot> trackSlots;
    QStringList lastSeeds;
    int lastSeedsVersion = -1;
    std::shared_ptr<JamWave> wave;
    QTimer reportTimer;
    bool reportPending = false;
};

}  // namespace Core
