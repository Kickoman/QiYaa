#pragma once

#include <QRect>
#include <QString>

#include <array>
#include <memory>
#include <span>
#include <vector>

class QPainter;

namespace Skins {
class Skin;
}  // namespace Skins

namespace Vis {

struct VisFrame {
    std::span<const float> left, right;
    std::span<const float> spectrum;
    int sampleRate = 44'100;
    int fftSize = 1024;
};

class Visualizer {
public:
    virtual ~Visualizer() = default;
    virtual QString name() const = 0;
    virtual void update(const VisFrame& frame) = 0;
    virtual void render(QPainter& painter, const QRect& area, const Skins::Skin& skin) const = 0;
    virtual void reset() { }
};

inline constexpr int kSpectrumBars = 19;

// One bar of the spectrum: its frequency range and the bins [firstBin, endBin) it covers.
struct SpectrumBand {
    double lowHz = 0;
    double highHz = 0;
    int firstBin = 0;
    int endBin = 0;
};

// The bars' bands: log-spaced from 60 Hz to min(16 kHz, sampleRate / 2), over the
// fftSize / 2 + 1 bins that Analyzer gives.
std::array<SpectrumBand, kSpectrumBars> SpectrumBands(int sampleRate, int fftSize);

// Each band's loudest bin, mapped from -72..-6 dB to 0..1: the bar heights of one frame, before
// the spectrum's fall-off and peaks are applied.
std::array<float, kSpectrumBars>
SpectrumLevels(std::span<const float> spectrumDb, int sampleRate, int fftSize);

std::unique_ptr<Visualizer> MakeSpectrum();
std::unique_ptr<Visualizer> MakeOscilloscope();

class Analyzer {
public:
    explicit Analyzer(int fftSize = 1024);
    int fftSize() const { return fftLength; }
    const std::vector<float>& analyze(std::span<const float> mono);

private:
    int fftLength;
    std::vector<float> windowFunction;
    std::vector<float> real, imaginary, decibels;
};

}  // namespace Vis
