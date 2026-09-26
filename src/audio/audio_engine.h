#pragma once

#include "audio/equalizer.h"
#include "audio/vis_tap.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>

namespace Audio {

class StreamBuffer;

class AudioEngine : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Buffering, Playing, Paused };
    Q_ENUM(State)

    explicit AudioEngine(QObject* parent = nullptr);
    ~AudioEngine() override;

    struct InitResult {
        bool ok = false;
        QString message;
    };
    InitResult init();
    QString backendName() const;

    using TStreamId = quint64;

    TStreamId beginStream();
    TStreamId queueStream();
    void clearQueued();
    TStreamId queuedStream() const;
    TStreamId playQueuedNow();

    void appendData(TStreamId stream, const QByteArray& bytes);
    void finishData(TStreamId stream);
    void failData(TStreamId stream);
    void appendData(const QByteArray& bytes) { appendData(currentId, bytes); }
    void finishData() { finishData(currentId); }
    void failData() { failData(currentId); }
    TStreamId currentStream() const { return currentId; }

    void pause();
    void resume();
    void stop();
    bool seek(double seconds);

    State state() const { return currentState; }
    double positionSeconds() const;
    int sourceSampleRate() const { return sourceRate.load(); }
    int sourceChannels() const { return sourceChannelCount.load(); }

    void setVolume(int percent);
    void setEqualizer(const EqSettings& settings);

    void readVisSamples(std::span<float> left, std::span<float> right) const;
    VisReadResult readNewVisSamples(uint32_t cursor, std::span<float> stereo) const;
    uint32_t visCursor() const;
    int outputSampleRate() const;
    void setBalance(int balance);

    void poll();

Q_SIGNALS:
    void stateChanged(Audio::AudioEngine::State state);
    void trackFinished();
    void trackAdvanced();
    void errorOccurred(const QString& message);

private:
    struct Implementation;
    void setState(State state);
    void updateGains();
    void startDecoder();
    void dropStreams();

    QHash<TStreamId, std::shared_ptr<StreamBuffer>> streams;
    TStreamId lastId = 0;
    TStreamId currentId = 0;
    TStreamId queuedId = 0;

    std::unique_ptr<Implementation> implementation;
    State currentState = State::Stopped;
    int volumePercent = 75;
    int balancePercent = 0;
    std::atomic<int> sourceRate{0};
    std::atomic<int> sourceChannelCount{0};
};

}  // namespace Audio
