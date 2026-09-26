#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <span>

namespace Audio {

inline constexpr int kEqBands = 10;
inline constexpr std::array<double, kEqBands> kEqBandHz = {60,   170,  310,    600,    1000,
                                                           3000, 6000, 12'000, 14'000, 16'000};
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

    // Writer and reader at once: only while process() cannot run.
    void setSampleRate(uint32_t rate);

    // UI thread.
    void publish(const EqSettings& settings);

    // Audio thread: interleaved stereo, in place.
    void process(std::span<float> stereoFrames);

    static double ResponseDb(const EqSettings& settings, double hz, double sampleRate);

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

    static Coeffs ComputeCoefficients(const EqSettings& settings, double sampleRate);

    double sampleRate = 44'100;
    EqSettings lastSettings;

    // Triple buffer: writer owns `back`, reader owns `front`, `middle` is exchanged.
    std::array<Coeffs, 3> coefficientSlots{};
    int back = 0;
    int front = 1;
    std::atomic<int> middle{2 | 0};  // index | (dirty << 2)

    // Audio thread only.
    std::array<std::array<float, 2>, kEqBands> filterState1{};
    std::array<std::array<float, 2>, kEqBands> filterState2{};
};

}  // namespace Audio
