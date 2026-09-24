// Visualization plug-in interface. The main window feeds every visualizer the
// same analysed frame; new visualizers (Milkdrop via projectM later) only need
// to implement this interface — the audio engine does not change.
#pragma once

#include <memory>
#include <span>
#include <vector>

#include <QRect>
#include <QString>

class QPainter;

namespace qiyaa {
class Skin;
}

namespace qiyaa::vis {

struct VisFrame {
    std::span<const float> left, right;  // latest PCM, -1..1
    std::span<const float> spectrum;     // magnitudes in dBFS, bins 0..N/2 (bin width = sampleRate / fftSize)
    int sampleRate = 44100;
    int fftSize = 1024;
};

class Visualizer {
public:
    virtual ~Visualizer() = default;
    virtual QString name() const = 0;
    // Called once per animation frame with fresh data.
    virtual void update(const VisFrame& frame) = 0;
    // Called whenever the window repaints; must only draw the last state.
    virtual void render(QPainter& p, const QRect& area, const Skin& skin) const = 0;
    virtual void reset() {}
};

// Winamp's classic 19-bar analyzer with falling peaks, colours from VISCOLOR.TXT.
std::unique_ptr<Visualizer> makeSpectrum();
// Winamp's oscilloscope ("lines" style).
std::unique_ptr<Visualizer> makeOscilloscope();

// Computes the spectrum for a frame (Hann window + radix-2 FFT).
class Analyzer {
public:
    explicit Analyzer(int fftSize = 1024);
    int fftSize() const { return m_size; }
    // `mono` must have fftSize() samples. Returns fftSize()/2 + 1 magnitudes in dBFS.
    const std::vector<float>& analyze(std::span<const float> mono);

private:
    int m_size;
    std::vector<float> m_window;
    std::vector<float> m_re, m_im, m_db;
};

}  // namespace qiyaa::vis
