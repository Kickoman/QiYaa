// Audio engine: network bytes -> decoder thread -> PCM ring buffer -> miniaudio device.
//
//   appendData() ──> StreamBuffer ──(decoder thread, ma_decoder)──> ma_pcm_rb ──> device callback
//                                                                             (EQ, VisTap,
//                                                                              volume/balance)
//
// Gapless: a second stream can be queued while the current one plays. When the
// current track's decoder reaches its end, the decoder thread continues with
// the queued stream into the same ring buffer, and poll() reports
// trackAdvanced() once playback crosses the boundary. A seek back into the old
// track before that undoes the chain.
//
// The device callback never blocks or allocates. Everything the UI needs
// (position, end of track) is read from atomics by poll(), which the UI calls
// from a timer only while something is playing.
#pragma once

#include "audio/equalizer.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

namespace Audio {

class StreamBuffer;

class AudioEngine : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Buffering, Playing, Paused };
    Q_ENUM(State)

    explicit AudioEngine(QObject* parent = nullptr);
    ~AudioEngine() override;

    // Opens the default output device. Safe to call once; returns false on failure.
    bool init(QString* error = nullptr);
    QString backendName() const;

    using TStreamId = quint64;  // 0 = none

    // Begin a new stream (drops the current and the queued one). Feed it with
    // appendData(), then finishData() once the download completes (or failData()).
    TStreamId beginStream();
    // The stream to continue with when the current one ends (replaces a queued
    // one). Only while something plays; returns 0 otherwise.
    TStreamId queueStream();
    // Forget the queued stream. If playback is already committed to it (the
    // last ~2 s of the current track), it stops at the boundary instead and
    // trackFinished() follows as usual.
    void clearQueued();
    TStreamId queuedStream() const;  // 0 if none (or it couldn't be decoded)
    // Start the queued stream now, from its beginning; returns its id (0 if none).
    TStreamId playQueuedNow();

    // Feeding a stream that was dropped meanwhile is a no-op.
    void appendData(TStreamId stream, const QByteArray& bytes);
    void finishData(TStreamId stream);
    void failData(TStreamId stream);
    // The same for the current stream.
    void appendData(const QByteArray& bytes) { appendData(current, bytes); }
    void finishData() { finishData(current); }
    void failData() { failData(current); }
    TStreamId currentStream() const { return current; }

    void pause();
    void resume();
    void stop();
    bool seek(double seconds);  // within the downloaded part

    State state() const { return currentState; }
    double positionSeconds() const;
    int sourceSampleRate() const { return sourceRate.load(); }
    int sourceChannels() const { return sourceChannelCount.load(); }

    void setVolume(int percent);  // 0..100
    void setEqualizer(const EqSettings& settings);

    // Latest `count` output frames (after EQ, before volume) for visualizations.
    void readVisSamples(float* left, float* right, uint32_t count) const;
    // Output frames played since `*cursor` (interleaved stereo, at most
    // `maxFrames`, the newest ones), for visualizations that want every sample
    // once (Milkdrop). Start with visCursor(). Returns the number of frames.
    uint32_t readNewVisSamples(uint32_t* cursor, float* stereo, uint32_t maxFrames) const;
    uint32_t visCursor() const;
    int outputSampleRate() const;
    void setBalance(int balance);  // -100 (left) .. 100 (right)

    // Publishes state changes and end of track; call from a UI timer.
    void poll();

Q_SIGNALS:
    void stateChanged(Audio::AudioEngine::State state);
    void trackFinished();
    // Playback moved on into the queued stream without a gap; it is current now.
    void trackAdvanced();
    void errorOccurred(const QString& message);

private:
    struct Impl;
    void setState(State s);
    void updateGains();
    void startDecoder();  // on current, with a fresh ring
    void dropStreams();

    // Streams that can still be fed, by id (UI thread only).
    QHash<TStreamId, std::shared_ptr<StreamBuffer>> streams;
    TStreamId lastId = 0;
    TStreamId current = 0;
    TStreamId queued = 0;

    std::unique_ptr<Impl> d;
    State currentState = State::Stopped;
    int volumePercent = 75;
    int balancePercent = 0;
    std::atomic<int> sourceRate{0};
    std::atomic<int> sourceChannelCount{0};
};

}  // namespace Audio
