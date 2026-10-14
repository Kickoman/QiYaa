#pragma once

#include "core/track.h"
#include "core/track_navigator.h"

#include <QList>
#include <QString>
#include <QtGlobal>

#include <memory>
#include <optional>

namespace Core {

class Queue {
public:
    const QList<Track>& tracks() const { return queued; }
    const QString& title() const { return titleText; }
    int cursor() const { return playingIndex; }
    const Track* currentTrack() const;

    void replace(const QList<Track>& tracks, const QString& title);
    bool append(const QList<Track>& tracks);
    bool insert(int index, const QList<Track>& tracks);
    bool remove(QList<int> indices);
    void setTitle(const QString& title) { titleText = title; }
    void setEndless(bool on);
    bool isEndless() const { return endless; }

    void select(int index);
    int next() const;
    int previous() const;

    void setShuffle(bool on);
    bool shuffle() const { return shuffleEnabled; }
    bool shuffleActive() const { return shuffleEnabled && !endless; }
    void setShuffleAlgorithm(ShuffleAlgorithm algorithm);
    ShuffleAlgorithm shuffleAlgorithm() const { return preferredAlgorithm; }
    void setRepeat(bool on) { repeatEnabled = on; }
    bool repeat() const { return repeatEnabled; }

    quint64 generation() const { return loadGeneration; }
    void renew();
    bool isAsking() const { return asking; }
    void setAsking(bool on) { asking = on; }

private:
    void resetNavigator();

    QList<Track> queued;
    QString titleText;
    int playingIndex = -1;
    std::unique_ptr<TrackNavigator> navigator = MakeTrackNavigator(std::nullopt);
    ShuffleAlgorithm preferredAlgorithm = ShuffleAlgorithm::WithoutRepeats;
    bool shuffleEnabled = false;
    bool repeatEnabled = false;
    bool endless = false;
    quint64 loadGeneration = 0;
    bool asking = false;
};

}  // namespace Core
