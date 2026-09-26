#pragma once

#include <QRect>
#include <QString>

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
