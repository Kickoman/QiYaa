#include "skins/skin.h"
#include "vis/visualizer.h"

#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace Vis {

// ------------------------------------------------------------------ Analyzer

Analyzer::Analyzer(int fftSize)
    : fftLength(fftSize)
    , windowFunction(fftSize)
    , real(fftSize)
    , imaginary(fftSize)
    , decibels(fftSize / 2 + 1) {
    for (int i = 0; i < fftSize; ++i) {
        windowFunction[i] =
            0.5f - 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * i / (fftSize - 1));
    }
}

const std::vector<float>& Analyzer::analyze(std::span<const float> mono) {
    const int n = fftLength;
    for (int i = 0; i < n; ++i) {
        real[i] = i < int(mono.size()) ? mono[i] * windowFunction[i] : 0.0f;
        imaginary[i] = 0.0f;
    }
    // Iterative radix-2 FFT.
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imaginary[i], imaginary[j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * std::numbers::pi_v<float> / len;
        const float wr = std::cos(ang), wi = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (int k = 0; k < len / 2; ++k) {
                const int a = i + k, b = i + k + len / 2;
                const float tr = real[b] * cr - imaginary[b] * ci;
                const float ti = real[b] * ci + imaginary[b] * cr;
                real[b] = real[a] - tr;
                imaginary[b] = imaginary[a] - ti;
                real[a] += tr;
                imaginary[a] += ti;
                const float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
    // A full-scale sine gives magnitude n/4 with a Hann window -> 0 dBFS.
    const float norm = 4.0f / n;
    for (int i = 0; i <= n / 2; ++i) {
        const float mag = std::sqrt(real[i] * real[i] + imaginary[i] * imaginary[i]) * norm;
        decibels[i] = 20.0f * std::log10(std::max(mag, 1e-9f));
    }
    return decibels;
}

namespace {

QColor VisColor(const Skins::Skin& skin, int i) {
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
        bars.fill(0);
        peaks.fill(0);
        peakFrames.fill(0);
    }

    void update(const VisFrame& frame) override {
        // Logarithmic buckets from ~60 Hz to ~16 kHz.
        const double binHz = double(frame.sampleRate) / frame.fftSize;
        const double lo = 60.0, hi = std::min(16000.0, frame.sampleRate / 2.0);
        for (int value = 0; value < kBars; ++value) {
            const double f0 = lo * std::pow(hi / lo, double(value) / kBars);
            const double f1 = lo * std::pow(hi / lo, double(value + 1) / kBars);
            int i0 = std::clamp(int(f0 / binHz), 1, int(frame.spectrum.size()) - 1);
            int i1 = std::clamp(int(std::ceil(f1 / binHz)), i0 + 1, int(frame.spectrum.size()));
            float peakDb = kMinDb;
            for (int i = i0; i < i1; ++i) {
                peakDb = std::max(peakDb, frame.spectrum[i]);
            }
            const float target = std::clamp((peakDb - kMinDb) / (kMaxDb - kMinDb), 0.0f, 1.0f);
            // Bars jump up and fall smoothly, like Winamp's "fast" falloff.
            bars[value] = std::max(target, bars[value] - 0.07f);
            float peak = peaks[value] - 0.0004f * peakFrames[value] * peakFrames[value];
            if (peak < bars[value]) {
                peak = bars[value];
                peakFrames[value] = 0;
            } else {
                ++peakFrames[value];
            }
            peaks[value] = std::max(peak, 0.0f);
        }
    }

    void render(QPainter& painter, const QRect& area, const Skins::Skin& skin) const override {
        const int h = area.height();
        for (int value = 0; value < kBars; ++value) {
            const int x = area.x() + value * 4;
            const int barH = int(std::ceil(bars[value] * h));
            for (int i = 0; i < barH; ++i) {
                // Colours 2..17: analyzer gradient from top to bottom.
                const int colorIndex = 2 + (h - 1 - i) * 16 / h;
                painter.fillRect(x, area.y() + h - 1 - i, 3, 1, VisColor(skin, colorIndex));
            }
            const int peakY = int(std::ceil(peaks[value] * h));
            if (peakY > 0) {
                painter.fillRect(x, area.y() + h - peakY, 3, 1, VisColor(skin, 23));
            }
        }
    }

private:
    std::array<float, kBars> bars{};
    std::array<float, kBars> peaks{};
    std::array<int, kBars> peakFrames{};
};

// ------------------------------------------------------------------ Oscilloscope

class Oscilloscope : public Visualizer {
public:
    QString name() const override { return QStringLiteral("Осциллограф"); }
    void reset() override { ys.fill(-1); }

    void update(const VisFrame& frame) override {
        const int n = int(frame.left.size());
        // Winamp shows ~576 samples across the 75 px area.
        const int window = std::min(n, 576);
        const int start = n - window;
        for (int x = 0; x < kWidth; ++x) {
            const int i = start + x * window / kWidth;
            const float v = 0.5f * (frame.left[i] + frame.right[i]);
            ys[x] = std::clamp(int(std::lround(7.5f - v * 8.0f)), 0, 15);
        }
    }

    void render(QPainter& painter, const QRect& area, const Skins::Skin& skin) const override {
        if (ys[0] < 0) {
            return;
        }
        int last = ys[0];
        for (int x = 0; x < kWidth && x < area.width(); ++x) {
            const int y = ys[x];
            const int top = std::min(last, y), bottom = std::max(last, y);
            for (int yy = top; yy <= bottom; ++yy) {
                // Colours 18..22: from the centre line outwards.
                const int dist = int(std::abs(yy - 7.5f));
                painter.fillRect(
                    area.x() + x, area.y() + yy, 1, 1, VisColor(skin, 18 + std::min(4, dist / 2))
                );
            }
            last = y;
        }
    }

private:
    static constexpr int kWidth = 75;
    std::array<int, kWidth> ys = [] {
        std::array<int, kWidth> a{};
        a.fill(-1);
        return a;
    }();
};

}  // namespace

std::unique_ptr<Visualizer> MakeSpectrum() {
    return std::make_unique<Spectrum>();
}
std::unique_ptr<Visualizer> MakeOscilloscope() {
    return std::make_unique<Oscilloscope>();
}

}  // namespace Vis
