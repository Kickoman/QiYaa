#include "audio/Equalizer.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace qiyaa::audio {

namespace {
// Roughly one octave wide; neighbouring Winamp bands overlap smoothly.
constexpr double kQ = 1.2;
constexpr int kDirty = 4;
}  // namespace

EqualizerDsp::EqualizerDsp() {
    m_slots[m_front] = compute(m_last, m_sampleRate);
}

void EqualizerDsp::setSampleRate(uint32_t rate) {
    m_sampleRate = rate > 0 ? rate : 44100;
    publish(m_last);
    // Make it current immediately (no audio thread yet).
    const int prev = m_middle.exchange(m_front);
    if (prev & kDirty) m_front = prev & 3;
}

EqualizerDsp::Coeffs EqualizerDsp::compute(const EqSettings& s, double sampleRate) {
    Coeffs c;
    c.enabled = s.enabled;
    c.preamp = float(std::pow(10.0, std::clamp(s.preampDb, -kEqMaxDb, kEqMaxDb) / 20.0));
    for (int i = 0; i < kEqBands; ++i) {
        Biquad& b = c.bands[i];
        const double db = std::clamp(s.bandsDb[i], -kEqMaxDb, kEqMaxDb);
        const double f0 = kEqBandHz[i];
        if (std::abs(db) < 0.05 || f0 >= sampleRate * 0.49) continue;  // identity
        // RBJ audio EQ cookbook, peaking EQ.
        const double A = std::pow(10.0, db / 40.0);
        const double w0 = 2.0 * std::numbers::pi * f0 / sampleRate;
        const double alpha = std::sin(w0) / (2.0 * kQ);
        const double cw = std::cos(w0);
        const double a0 = 1.0 + alpha / A;
        b.b0 = float((1.0 + alpha * A) / a0);
        b.b1 = float((-2.0 * cw) / a0);
        b.b2 = float((1.0 - alpha * A) / a0);
        b.a1 = float((-2.0 * cw) / a0);
        b.a2 = float((1.0 - alpha / A) / a0);
        b.identity = false;
    }
    return c;
}

void EqualizerDsp::publish(const EqSettings& settings) {
    m_last = settings;
    m_slots[m_back] = compute(settings, m_sampleRate);
    const int prev = m_middle.exchange(m_back | kDirty, std::memory_order_acq_rel);
    m_back = prev & 3;
}

void EqualizerDsp::process(float* frames, uint32_t frameCount) {
    if (m_middle.load(std::memory_order_relaxed) & kDirty) {
        const int prev = m_middle.exchange(m_front, std::memory_order_acq_rel);
        m_front = prev & 3;
    }
    const Coeffs& c = m_slots[m_front];
    if (!c.enabled) return;

    if (c.preamp != 1.0f)
        for (uint32_t i = 0; i < frameCount * 2; ++i) frames[i] *= c.preamp;

    for (int band = 0; band < kEqBands; ++band) {
        const Biquad& b = c.bands[band];
        if (b.identity) {
            // Let the state decay so re-enabling a band doesn't pop.
            m_z1[band] = {0, 0};
            m_z2[band] = {0, 0};
            continue;
        }
        for (int ch = 0; ch < 2; ++ch) {
            float z1 = m_z1[band][ch], z2 = m_z2[band][ch];
            float* p = frames + ch;
            for (uint32_t i = 0; i < frameCount; ++i, p += 2) {
                // Transposed direct form II.
                const float x = *p;
                const float y = b.b0 * x + z1;
                z1 = b.b1 * x - b.a1 * y + z2;
                z2 = b.b2 * x - b.a2 * y;
                *p = y;
            }
            // Flush denormals.
            m_z1[band][ch] = std::abs(z1) < 1e-15f ? 0.0f : z1;
            m_z2[band][ch] = std::abs(z2) < 1e-15f ? 0.0f : z2;
        }
    }
}

double EqualizerDsp::responseDb(const EqSettings& s, double hz, double sampleRate) {
    const Coeffs c = compute(s, sampleRate);
    if (!c.enabled) return 0.0;
    const std::complex<double> z = std::polar(1.0, -2.0 * std::numbers::pi * hz / sampleRate);  // z^-1
    std::complex<double> h = c.preamp;
    for (const Biquad& b : c.bands) {
        if (b.identity) continue;
        h *= (double(b.b0) + double(b.b1) * z + double(b.b2) * z * z) / (1.0 + double(b.a1) * z + double(b.a2) * z * z);
    }
    return 20.0 * std::log10(std::abs(h));
}

}  // namespace qiyaa::audio
