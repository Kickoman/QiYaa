#include "audio/equalizer.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace Audio {

namespace {
constexpr double kQ = 1.2;
constexpr int kDirty = 4;
}  // namespace

EqualizerDsp::EqualizerDsp() {
    coefficientSlots[front] = ComputeCoefficients(lastSettings, sampleRate);
}

void EqualizerDsp::setSampleRate(uint32_t rate) {
    sampleRate = rate > 0 ? rate : 44100;
    publish(lastSettings);
    // No audio thread yet: make it current now.
    const int prev = middle.exchange(front);
    if (prev & kDirty) {
        front = prev & 3;
    }
}

EqualizerDsp::Coeffs
EqualizerDsp::ComputeCoefficients(const EqSettings& settings, double sampleRate) {
    Coeffs c;
    c.enabled = settings.enabled;
    c.preamp = float(std::pow(10.0, std::clamp(settings.preampDb, -kEqMaxDb, kEqMaxDb) / 20.0));
    for (int i = 0; i < kEqBands; ++i) {
        Biquad& filter = c.bands[i];
        const double db = std::clamp(settings.bandsDb[i], -kEqMaxDb, kEqMaxDb);
        const double f0 = kEqBandHz[i];
        if (std::abs(db) < 0.05 || f0 >= sampleRate * 0.49) {
            continue;
        }
        // RBJ audio EQ cookbook, peaking EQ.
        const double A = std::pow(10.0, db / 40.0);
        const double w0 = 2.0 * std::numbers::pi * f0 / sampleRate;
        const double alpha = std::sin(w0) / (2.0 * kQ);
        const double cw = std::cos(w0);
        const double a0 = 1.0 + alpha / A;
        filter.b0 = float((1.0 + alpha * A) / a0);
        filter.b1 = float((-2.0 * cw) / a0);
        filter.b2 = float((1.0 - alpha * A) / a0);
        filter.a1 = float((-2.0 * cw) / a0);
        filter.a2 = float((1.0 - alpha / A) / a0);
        filter.identity = false;
    }
    return c;
}

void EqualizerDsp::publish(const EqSettings& settings) {
    lastSettings = settings;
    coefficientSlots[back] = ComputeCoefficients(settings, sampleRate);
    const int prev = middle.exchange(back | kDirty, std::memory_order_acq_rel);
    back = prev & 3;
}

void EqualizerDsp::process(float* frames, uint32_t frameCount) {
    if (middle.load(std::memory_order_relaxed) & kDirty) {
        const int prev = middle.exchange(front, std::memory_order_acq_rel);
        front = prev & 3;
    }
    const Coeffs& c = coefficientSlots[front];
    if (!c.enabled) {
        return;
    }

    if (c.preamp != 1.0f) {
        for (uint32_t i = 0; i < frameCount * 2; ++i) {
            frames[i] *= c.preamp;
        }
    }

    for (int band = 0; band < kEqBands; ++band) {
        const Biquad& filter = c.bands[band];
        if (filter.identity) {
            filterState1[band] = {0, 0};
            filterState2[band] = {0, 0};
            continue;
        }
        for (int ch = 0; ch < 2; ++ch) {
            float z1 = filterState1[band][ch], z2 = filterState2[band][ch];
            float* p = frames + ch;
            for (uint32_t i = 0; i < frameCount; ++i, p += 2) {
                // Transposed direct form II.
                const float x = *p;
                const float y = filter.b0 * x + z1;
                z1 = filter.b1 * x - filter.a1 * y + z2;
                z2 = filter.b2 * x - filter.a2 * y;
                *p = y;
            }
            // Flush denormals.
            filterState1[band][ch] = std::abs(z1) < 1e-15f ? 0.0f : z1;
            filterState2[band][ch] = std::abs(z2) < 1e-15f ? 0.0f : z2;
        }
    }
}

double EqualizerDsp::ResponseDb(const EqSettings& settings, double hz, double sampleRate) {
    const Coeffs c = ComputeCoefficients(settings, sampleRate);
    if (!c.enabled) {
        return 0.0;
    }
    const std::complex<double> z =
        std::polar(1.0, -2.0 * std::numbers::pi * hz / sampleRate);  // z^-1
    std::complex<double> h = c.preamp;
    for (const Biquad& filter : c.bands) {
        if (filter.identity) {
            continue;
        }
        h *= (double(filter.b0) + double(filter.b1) * z + double(filter.b2) * z * z)
            / (1.0 + double(filter.a1) * z + double(filter.a2) * z * z);
    }
    return 20.0 * std::log10(std::abs(h));
}

}  // namespace Audio
