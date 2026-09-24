#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include <QPainter>

#include "skin/Skin.h"
#include "vis/Visualizer.h"

namespace qiyaa::vis {

// ------------------------------------------------------------------ Analyzer

Analyzer::Analyzer(int fftSize) : m_size(fftSize), m_window(fftSize), m_re(fftSize), m_im(fftSize), m_db(fftSize / 2 + 1) {
    for (int i = 0; i < fftSize; ++i)
        m_window[i] = 0.5f - 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * i / (fftSize - 1));
}

const std::vector<float>& Analyzer::analyze(std::span<const float> mono) {
    const int n = m_size;
    for (int i = 0; i < n; ++i) {
        m_re[i] = i < int(mono.size()) ? mono[i] * m_window[i] : 0.0f;
        m_im[i] = 0.0f;
    }
    // Iterative radix-2 FFT.
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(m_re[i], m_re[j]);
            std::swap(m_im[i], m_im[j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * std::numbers::pi_v<float> / len;
        const float wr = std::cos(ang), wi = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (int k = 0; k < len / 2; ++k) {
                const int a = i + k, b = i + k + len / 2;
                const float tr = m_re[b] * cr - m_im[b] * ci;
                const float ti = m_re[b] * ci + m_im[b] * cr;
                m_re[b] = m_re[a] - tr;
                m_im[b] = m_im[a] - ti;
                m_re[a] += tr;
                m_im[a] += ti;
                const float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
    // A full-scale sine gives magnitude n/4 with a Hann window -> 0 dBFS.
    const float norm = 4.0f / n;
    for (int i = 0; i <= n / 2; ++i) {
        const float mag = std::sqrt(m_re[i] * m_re[i] + m_im[i] * m_im[i]) * norm;
        m_db[i] = 20.0f * std::log10(std::max(mag, 1e-9f));
    }
    return m_db;
}

namespace {

QColor visColor(const Skin& skin, int i) {
    const auto& c = skin.visColors();
    return i < c.size() ? c[i] : QColor(Qt::green);
}

// ------------------------------------------------------------------ Spectrum

class Spectrum : public Visualizer {
public:
    static constexpr int kBars = 19;
    static constexpr float kMinDb = -72.0f, kMaxDb = -6.0f;

    QString name() const override { return QStringLiteral("Спектр"); }

    void reset() override {
        m_bars.fill(0);
        m_peaks.fill(0);
        m_peakFrames.fill(0);
    }

    void update(const VisFrame& f) override {
        // Logarithmic buckets from ~60 Hz to ~16 kHz.
        const double binHz = double(f.sampleRate) / f.fftSize;
        const double lo = 60.0, hi = std::min(16000.0, f.sampleRate / 2.0);
        for (int b = 0; b < kBars; ++b) {
            const double f0 = lo * std::pow(hi / lo, double(b) / kBars);
            const double f1 = lo * std::pow(hi / lo, double(b + 1) / kBars);
            int i0 = std::clamp(int(f0 / binHz), 1, int(f.spectrum.size()) - 1);
            int i1 = std::clamp(int(std::ceil(f1 / binHz)), i0 + 1, int(f.spectrum.size()));
            float peakDb = kMinDb;
            for (int i = i0; i < i1; ++i) peakDb = std::max(peakDb, f.spectrum[i]);
            const float target = std::clamp((peakDb - kMinDb) / (kMaxDb - kMinDb), 0.0f, 1.0f);
            // Bars jump up and fall smoothly, like Winamp's "fast" falloff.
            m_bars[b] = std::max(target, m_bars[b] - 0.07f);
            float peak = m_peaks[b] - 0.0004f * m_peakFrames[b] * m_peakFrames[b];
            if (peak < m_bars[b]) {
                peak = m_bars[b];
                m_peakFrames[b] = 0;
            } else {
                ++m_peakFrames[b];
            }
            m_peaks[b] = std::max(peak, 0.0f);
        }
    }

    void render(QPainter& p, const QRect& area, const Skin& skin) const override {
        const int h = area.height();
        for (int b = 0; b < kBars; ++b) {
            const int x = area.x() + b * 4;
            const int barH = int(std::ceil(m_bars[b] * h));
            for (int i = 0; i < barH; ++i) {
                // Colours 2..17: analyzer gradient from top to bottom.
                const int colorIndex = 2 + (h - 1 - i) * 16 / h;
                p.fillRect(x, area.y() + h - 1 - i, 3, 1, visColor(skin, colorIndex));
            }
            const int peakY = int(std::ceil(m_peaks[b] * h));
            if (peakY > 0) p.fillRect(x, area.y() + h - peakY, 3, 1, visColor(skin, 23));
        }
    }

private:
    std::array<float, kBars> m_bars{};
    std::array<float, kBars> m_peaks{};
    std::array<int, kBars> m_peakFrames{};
};

// ------------------------------------------------------------------ Oscilloscope

class Oscilloscope : public Visualizer {
public:
    QString name() const override { return QStringLiteral("Осциллограф"); }
    void reset() override { m_ys.fill(-1); }

    void update(const VisFrame& f) override {
        const int n = int(f.left.size());
        // Winamp shows ~576 samples across the 75 px area.
        const int window = std::min(n, 576);
        const int start = n - window;
        for (int x = 0; x < kWidth; ++x) {
            const int i = start + x * window / kWidth;
            const float v = 0.5f * (f.left[i] + f.right[i]);
            m_ys[x] = std::clamp(int(std::lround(7.5f - v * 8.0f)), 0, 15);
        }
    }

    void render(QPainter& p, const QRect& area, const Skin& skin) const override {
        if (m_ys[0] < 0) return;
        int last = m_ys[0];
        for (int x = 0; x < kWidth && x < area.width(); ++x) {
            const int y = m_ys[x];
            const int top = std::min(last, y), bottom = std::max(last, y);
            for (int yy = top; yy <= bottom; ++yy) {
                // Colours 18..22: from the centre line outwards.
                const int dist = int(std::abs(yy - 7.5f));
                p.fillRect(area.x() + x, area.y() + yy, 1, 1, visColor(skin, 18 + std::min(4, dist / 2)));
            }
            last = y;
        }
    }

private:
    static constexpr int kWidth = 75;
    std::array<int, kWidth> m_ys = [] {
        std::array<int, kWidth> a{};
        a.fill(-1);
        return a;
    }();
};

}  // namespace

std::unique_ptr<Visualizer> makeSpectrum() { return std::make_unique<Spectrum>(); }
std::unique_ptr<Visualizer> makeOscilloscope() { return std::make_unique<Oscilloscope>(); }

}  // namespace qiyaa::vis
