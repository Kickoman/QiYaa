#pragma once

#include "core/player.h"
#include "core/queue_source.h"
#include "core/track.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <optional>

namespace Core {

using JamEntry = LayerItem;

struct JamPlayback {
    enum class Kind { Item, Base, Idle };
    Kind kind = Kind::Idle;
    QString itemId;
    std::optional<Track> track;
    qint64 positionMs = 0;
    bool paused = true;
    QUrl link;  // the current file, for guests who listen along (spec/jam/listen.md)
    QUrl nextLink;  // the preloaded next file
};

inline constexpr int kJamPlayingReportMs = 10'000;

bool IsJamItem(const QueueEntry* entry);

// The jam over the host's own queue (spec/jam/host.md): the guests' items as the Player's layer,
// the host's source under them as the base, and what the host does not learn.
class JamMode : public QObject {
    Q_OBJECT
public:
    explicit JamMode(Player* player, QObject* parent = nullptr);
    ~JamMode() override;

    bool isActive() const { return active; }

    void start(bool waveFeedback);
    void setQueue(const QList<JamEntry>& entries, const QStringList& seeds, int seedsVersion);
    void skip(const QString& itemId);
    void end();
    void reportPlayback();
    void setPlayingReportInterval(int ms) { reportTimer.setInterval(ms); }

Q_SIGNALS:
    void itemStarted(const QString& itemId);
    void playback(const Core::JamPlayback& playback);
    void removeRequested(const QStringList& itemIds);

private:
    bool nothingToPlay() const;
    void schedulePlaybackReport();

    Player* corePlayer;
    bool active = false;
    QTimer reportTimer;
    bool reportPending = false;
};

}  // namespace Core
