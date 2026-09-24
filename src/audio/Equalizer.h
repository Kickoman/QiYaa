// Winamp-style 10-band equalizer: preamp + 10 peaking biquads per channel.
//
// The UI thread calls publish() with new settings; coefficients are computed
// there and handed to the audio thread through a lock-free triple buffer, so
// process() never blocks or allocates.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace qiyaa::audio {

inline constexpr int kEqBands = 10;
inline constexpr std::array<double, kEqBands> kEqBandHz = {60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000};
inline constexpr double kEqMaxDb = 12.0;

struct EqSettings {
    bool enabled = true;
    double preampDb = 0.0;
    std::array<double, kEqBands> bandsDb{};

    bool operator==(const EqSettings&) const = default;
};

class EqualizerDsp {
public:
    EqualizerDsp();

    // Both must be called before the audio thread starts or from it; the rate
    // is fixed for the lifetime of the device.
    void setSampleRate(uint32_t rate);

    // UI thread.
    void publish(const EqSettings& settings);

    // Audio thread: interleaved stereo, in place.
    void process(float* frames, uint32_t frameCount);

    // Magnitude response in dB at `hz` for the given settings (used by tests).
    static double responseDb(const EqSettings& s, double hz, double sampleRate);

private:
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        bool identity = true;
    };
    struct Coeffs {
        bool enabled = false;
        float preamp = 1.0f;
        std::array<Biquad, kEqBands> bands{};
    };

    static Coeffs compute(const EqSettings& s, double sampleRate);

    double m_sampleRate = 44100;
    EqSettings m_last;

    // Triple buffer: writer owns `back`, reader owns `front`, `middle` is exchanged.
    std::array<Coeffs, 3> m_slots{};
    int m_back = 0;                       // writer only
    int m_front = 1;                      // reader only
    std::atomic<int> m_middle{2 | 0};    // index | (dirty << 2)

    // Filter state per band per channel (audio thread only).
    std::array<std::array<float, 2>, kEqBands> m_z1{};
    std::array<std::array<float, 2>, kEqBands> m_z2{};
};

}  // namespace qiyaa::audio
