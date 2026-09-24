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

#include <atomic>
#include <memory>

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

#include "audio/Equalizer.h"

namespace qiyaa::audio {

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

    using StreamId = quint64;  // 0 = none

    // Begin a new stream (drops the current and the queued one). Feed it with
    // appendData(), then finishData() once the download completes (or failData()).
    StreamId beginStream();
    // The stream to continue with when the current one ends (replaces a queued
    // one). Only while something plays; returns 0 otherwise.
    StreamId queueStream();
    // Forget the queued stream. If playback is already committed to it (the
    // last ~2 s of the current track), it stops at the boundary instead and
    // trackFinished() follows as usual.
    void clearQueued();
    StreamId queuedStream() const;  // 0 if none (or it couldn't be decoded)
    // Start the queued stream now, from its beginning; returns its id (0 if none).
    StreamId playQueuedNow();

    // Feeding a stream that was dropped meanwhile is a no-op.
    void appendData(StreamId stream, const QByteArray& bytes);
    void finishData(StreamId stream);
    void failData(StreamId stream);
    // The same for the current stream.
    void appendData(const QByteArray& bytes) { appendData(m_current, bytes); }
    void finishData() { finishData(m_current); }
    void failData() { failData(m_current); }
    StreamId currentStream() const { return m_current; }

    void pause();
    void resume();
    void stop();
    bool seek(double seconds);  // within the downloaded part

    State state() const { return m_state; }
    double positionSeconds() const;
    int sourceSampleRate() const { return m_sourceRate.load(); }
    int sourceChannels() const { return m_sourceChannels.load(); }

    void setVolume(int percent);    // 0..100
    void setEqualizer(const EqSettings& settings);

    // Latest `count` output frames (after EQ, before volume) for visualizations.
    void readVisSamples(float* left, float* right, uint32_t count) const;
    // Output frames played since `*cursor` (interleaved stereo, at most
    // `maxFrames`, the newest ones), for visualizations that want every sample
    // once (Milkdrop). Start with visCursor(). Returns the number of frames.
    uint32_t readNewVisSamples(uint32_t* cursor, float* stereo, uint32_t maxFrames) const;
    uint32_t visCursor() const;
    int outputSampleRate() const;
    void setBalance(int balance);   // -100 (left) .. 100 (right)

    // Publishes state changes and end of track; call from a UI timer.
    void poll();

Q_SIGNALS:
    void stateChanged(qiyaa::audio::AudioEngine::State state);
    void trackFinished();
    // Playback moved on into the queued stream without a gap; it is current now.
    void trackAdvanced();
    void errorOccurred(const QString& message);

private:
    struct Impl;
    void setState(State s);
    void updateGains();
    void startDecoder();  // on m_current, with a fresh ring
    void dropStreams();

    // Streams that can still be fed, by id (UI thread only).
    QHash<StreamId, std::shared_ptr<StreamBuffer>> m_streams;
    StreamId m_lastId = 0;
    StreamId m_current = 0;
    StreamId m_queued = 0;

    std::unique_ptr<Impl> d;
    State m_state = State::Stopped;
    int m_volume = 75;
    int m_balance = 0;
    std::atomic<int> m_sourceRate{0};
    std::atomic<int> m_sourceChannels{0};
};

}  // namespace qiyaa::audio
