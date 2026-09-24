// Audio engine: network bytes -> decoder thread -> PCM ring buffer -> miniaudio device.
//
//   appendData() ──> StreamBuffer ──(decoder thread, ma_decoder)──> ma_pcm_rb ──> device callback
//                                                                             (volume/balance;
//                                                                              later: EQ, VisTap)
//
// The device callback never blocks or allocates. Everything the UI needs
// (position, end of track) is read from atomics by poll(), which the UI calls
// from a timer only while something is playing.
#pragma once

#include <atomic>
#include <memory>

#include <QByteArray>
#include <QObject>
#include <QString>

namespace qiyaa::audio {

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

    // Begin a new stream (drops the current one). Feed it with appendData(),
    // then finishData() once the download completes (or failData() on error).
    void beginStream();
    void appendData(const QByteArray& bytes);
    void finishData();
    void failData();

    void pause();
    void resume();
    void stop();
    bool seek(double seconds);  // within the downloaded part

    State state() const { return m_state; }
    double positionSeconds() const;
    int sourceSampleRate() const { return m_sourceRate.load(); }
    int sourceChannels() const { return m_sourceChannels.load(); }

    void setVolume(int percent);    // 0..100
    void setBalance(int balance);   // -100 (left) .. 100 (right)

    // Publishes state changes and end of track; call from a UI timer.
    void poll();

Q_SIGNALS:
    void stateChanged(qiyaa::audio::AudioEngine::State state);
    void trackFinished();
    void errorOccurred(const QString& message);

private:
    struct Impl;
    void setState(State s);
    void updateGains();

    std::unique_ptr<Impl> d;
    State m_state = State::Stopped;
    int m_volume = 75;
    int m_balance = 0;
    std::atomic<int> m_sourceRate{0};
    std::atomic<int> m_sourceChannels{0};
};

}  // namespace qiyaa::audio
