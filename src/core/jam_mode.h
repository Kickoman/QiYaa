#pragma once

#include "core/player.h"
#include "yandex/api_client.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <optional>

namespace Yandex {
class Library;
}  // namespace Yandex

namespace Core {

struct JamEntry {
    QString itemId;
    Yandex::Track track;
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
    std::optional<Yandex::Track> track;
    qint64 positionMs = 0;
    bool paused = true;
    QUrl link;  // the current file, for guests who listen along (spec/jam/listen.md)
    QUrl nextLink;  // the preloaded next file
};

inline constexpr int kJamPlayingReportMs = 10'000;
inline constexpr int kJamWaveHistory = 5;

// The jam mode of the queue (spec/jam/host.md, HOST-01 to HOST-17, HOST-32, HOST-33): the
// current track, then the jam part (the mirror of the server's queue), then the jam wave.
class JamMode : public QObject {
    Q_OBJECT
public:
    JamMode(Player* player, Yandex::Library* library, QObject* parent = nullptr);
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
    struct Wave;

    void loadWave(std::function<void(const QList<Yandex::Track>&)> done);
    void appendWave(
        const QList<Yandex::Track>& tracks,
        std::function<void(const QList<Yandex::Track>&)> done
    );
    void dropDeferredWave();
    void removeSlots(const QList<int>& indices);
    void insertItems(int at, const QList<JamEntry>& entries);
    void checkSlots();
    void schedulePlaybackReport();

    Player* corePlayer;
    Yandex::Library* yandexLibrary;
    bool active = false;
    QList<JamSlot> trackSlots;
    QStringList lastSeeds;
    int lastSeedsVersion = -1;
    std::shared_ptr<Wave> wave;
    QTimer reportTimer;
    bool reportPending = false;
};

}  // namespace Core
