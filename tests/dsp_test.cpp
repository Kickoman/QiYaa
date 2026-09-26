#include "audio/eq_presets.h"
#include "audio/equalizer.h"
#include "audio/error.h"
#include "skins/skin.h"
#include "ui/equalizer_window.h"
#include "vis/visualizer.h"

#include <QImage>
#include <QList>
#include <QObject>
#include <QPainter>
#include <QRgb>
#include <QString>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>
#include <vector>

namespace {
std::vector<float> StereoSine(double hz, double sampleRate, int frames, float amplitude = 0.5f) {
    std::vector<float> samples(frames * 2);
    for (int i = 0; i < frames; ++i) {
        const float sample =
            amplitude * float(std::sin(2 * std::numbers::pi * hz * i / sampleRate));
        samples[i * 2] = samples[i * 2 + 1] = sample;
    }
    return samples;
}

double Rms(const std::vector<float>& samples, int fromFrame) {
    double sum = 0;
    int frameCount = 0;
    for (size_t i = fromFrame * 2; i < samples.size(); i += 2, ++frameCount) {
        sum += double(samples[i]) * samples[i];
    }
    return std::sqrt(sum / frameCount);
}
}  // namespace

class TestDsp : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void flatIsTransparent() {
        Audio::EqSettings settings;
        for (double hz : {30.0, 60.0, 1000.0, 10'000.0, 18'000.0}) {
            QVERIFY(std::abs(Audio::EqualizerDsp::ResponseDb(settings, hz, 44'100)) < 0.01);
        }
    }

    void bandBoostHitsItsFrequency() {
        Audio::EqSettings settings;
        settings.bandsDb[4] = 12;  // 1 kHz
        QVERIFY(std::abs(Audio::EqualizerDsp::ResponseDb(settings, 1000, 44'100) - 12) < 0.1);
        QVERIFY(std::abs(Audio::EqualizerDsp::ResponseDb(settings, 60, 44'100)) < 0.5);
        QVERIFY(std::abs(Audio::EqualizerDsp::ResponseDb(settings, 16'000, 44'100)) < 0.5);
    }

    void preampShiftsTheResponseAndDisablingMakesItFlat() {
        Audio::EqSettings settings;
        settings.preampDb = -6;
        QVERIFY(std::abs(Audio::EqualizerDsp::ResponseDb(settings, 1000, 48'000) + 6) < 0.01);
        settings.enabled = false;
        QCOMPARE(Audio::EqualizerDsp::ResponseDb(settings, 1000, 48'000), 0.0);
    }

    void processingMatchesResponse() {
        Audio::EqualizerDsp lowEq;
        lowEq.setSampleRate(44'100);
        Audio::EqSettings settings;
        settings.bandsDb[0] = -12;  // 60 Hz cut
        lowEq.publish(settings);
        auto lowTone = StereoSine(60, 44'100, 44'100);
        auto midTone = StereoSine(3000, 44'100, 44'100);
        const double lowInputRms = Rms(lowTone, 4410), midInputRms = Rms(midTone, 4410);
        lowEq.process(lowTone);
        Audio::EqualizerDsp midEq;
        midEq.setSampleRate(44'100);
        midEq.publish(settings);
        midEq.process(midTone);
        const double lowDb = 20 * std::log10(Rms(lowTone, 4410) / lowInputRms);
        const double midDb = 20 * std::log10(Rms(midTone, 4410) / midInputRms);
        QVERIFY2(std::abs(lowDb + 12) < 0.5, qPrintable(QString::number(lowDb)));
        QVERIFY2(std::abs(midDb) < 0.3, qPrintable(QString::number(midDb)));
    }

    void settingsChangingWhileProcessingKeepsOutputBounded() {
        Audio::EqualizerDsp eq;
        eq.setSampleRate(48'000);
        auto samples = StereoSine(440, 48'000, 48'000);
        for (int block = 0; block < 100; ++block) {
            Audio::EqSettings settings;
            settings.bandsDb[block % 10] = (block % 2 ? 12 : -12);
            eq.publish(settings);
            eq.process(std::span(samples).subspan(block * 480 * 2, 480 * 2));
        }
        for (float sample : samples) {
            QVERIFY(std::isfinite(sample) && std::abs(sample) < 4.0f);
        }
    }

    void presetsLookRight() {
        const auto presets = Audio::BuiltinEqPresets();
        QCOMPARE(presets.size(), 17);
        const auto bass =
            std::find_if(presets.cbegin(), presets.cend(), [](const Audio::EqPreset& preset) {
                return preset.name == "Full Bass";
            });
        QVERIFY(bass != presets.cend());
        QVERIFY(bass->settings.bandsDb[0] > 5);
        QVERIFY(bass->settings.bandsDb[9] < -5);
        QCOMPARE(Audio::EqfToDb(1), -12.0);
        QCOMPARE(Audio::EqfToDb(64), 12.0);
    }

    void eqfRoundTripKeepsNamesAndLevels() {
        Audio::EqPreset preset;
        preset.name = QStringLiteral("My EQ");
        for (int i = 0; i < Audio::kEqBands; ++i) {
            preset.settings.bandsDb[i] = -12.0 + i * 2.6;
        }
        preset.settings.preampDb = 3.0;
        const QByteArray file = Audio::WriteEqf({preset, Audio::BuiltinEqPresets().first()});
        QCOMPARE(file.size(), 31 + 2 * 268);  // header + 2 presets (name 257 + 11 values)
        QVERIFY(file.startsWith("Winamp EQ library file v1.1\x1a!--"));
        const QList<Audio::EqPreset> readBack = Audio::ParseEqf(file);
        QCOMPARE(readBack.size(), 2);
        QCOMPARE(readBack[0].name, QStringLiteral("My EQ"));
        for (int i = 0; i < Audio::kEqBands; ++i) {
            // 64 levels over 24 dB: half a step (0.19 dB) + rounding to 0.1 dB.
            QVERIFY2(
                std::abs(readBack[0].settings.bandsDb[i] - preset.settings.bandsDb[i]) <= 0.25,
                qPrintable(QString::number(i))
            );
        }
        QVERIFY(std::abs(readBack[0].settings.preampDb - 3.0) <= 0.25);
        QCOMPARE(readBack[1].name, QStringLiteral("Classical"));
    }

    void eqfStoresMaxAsZeroAndMinAsSixtyThree() {
        // Max +12 dB is stored as 0, min -12 dB as 63 (webamp's max/min sample files).
        QByteArray file("Winamp EQ library file v1.1\x1a!--");
        QByteArray name("max");
        name.append(QByteArray(257 - name.size(), '\0'));
        file += name + QByteArray(10, char(0)) + QByteArray(1, char(63));
        const QList<Audio::EqPreset> presets = Audio::ParseEqf(file);
        QCOMPARE(presets[0].settings.bandsDb[0], 12.0);
        QCOMPARE(presets[0].settings.preampDb, -12.0);
    }

    void eqfParserRejectsWhatIsNotAPreset() {
        QVERIFY_THROWS_EXCEPTION(Audio::Error, Audio::ParseEqf("not an eqf"));
        // The header alone: a library without presets.
        QVERIFY_THROWS_EXCEPTION(
            Audio::Error, Audio::ParseEqf(QByteArray("Winamp EQ library file v1.1\x1a!--", 31))
        );
        try {
            Audio::ParseEqf("not an eqf");
        } catch (const Audio::Error& error) {
            QVERIFY2(QByteArray(error.what()).contains("10 bytes"), error.what());
        }
    }

    void graphSplinePassesThroughBands() {
        Audio::EqSettings settings;
        settings.bandsDb[3] = 12;
        const QList<double> curve = Ui::EqualizerWindow::GraphCurve(settings);
        QCOMPARE(curve.size(), 9 * 12 + 1);
        QVERIFY(std::abs(curve[3 * 12] - 0) < 1e-6);  // +12 dB = top row
        QVERIFY(std::abs(curve[0] - 9) < 1e-6);  // 0 dB = middle
        QVERIFY(curve[30] < 9 && curve[42] < 9);
    }

    void analyzerFindsTheTone() {
        Vis::Analyzer analyzer(1024);
        const double sampleRate = 44'100, hz = 1000;
        std::vector<float> mono(1024);
        for (int i = 0; i < 1024; ++i) {
            mono[i] = float(std::sin(2 * std::numbers::pi * hz * i / sampleRate));
        }
        const auto& spectrumDb = analyzer.analyze(mono);
        QCOMPARE(int(spectrumDb.size()), 513);
        const int peak =
            int(std::max_element(spectrumDb.begin(), spectrumDb.end()) - spectrumDb.begin());
        QCOMPARE(peak, int(std::lround(hz / (sampleRate / 1024))));
        QVERIFY(std::abs(spectrumDb[peak]) < 1.5);  // full-scale sine ~ 0 dBFS
        QVERIFY(spectrumDb[400] < -50);
    }

    void spectrumAndScopeRender() {
        const Skins::Skin skin = Skins::Skin::BuiltinBase();
        Vis::Analyzer analyzer(1024);
        std::vector<float> left(1024), right(1024), mono(1024);
        for (int i = 0; i < 1024; ++i) {
            left[i] = right[i] = mono[i] =
                0.8f * float(std::sin(2 * std::numbers::pi * 200 * i / 44'100.0));
        }
        const auto& spectrumDb = analyzer.analyze(mono);
        Vis::VisFrame frame{left, right, spectrumDb, 44'100, 1024};

        for (auto factory : {Vis::MakeSpectrum, Vis::MakeOscilloscope}) {
            auto visualizer = factory();
            QImage image(76, 16, QImage::Format_ARGB32);
            image.fill(Qt::black);
            visualizer->update(frame);
            QPainter painter(&image);
            visualizer->render(painter, QRect(0, 0, 76, 16), skin);
            painter.end();
            int litPixels = 0;
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 76; ++x) {
                    litPixels += image.pixel(x, y) != qRgb(0, 0, 0);
                }
            }
            QVERIFY2(litPixels > 10, qPrintable(visualizer->name()));
        }
    }

    void eqfReadsCentreAsZeroAndClampsOutOfRangeBytes() {
        QByteArray file("Winamp EQ library file v1.1\x1a!--", 31);
        QByteArray name("Flat");
        name.append(QByteArray(257 - name.size(), '\0'));
        file += name;
        file += QByteArray(10, char(31));  // Winamp's own midline.EQF: 64 - 31 = 33
        file += char(255);  // garbage: must not reach the DSP as -85 dB
        const QList<Audio::EqPreset> presets = Audio::ParseEqf(file);
        for (int band = 0; band < Audio::kEqBands; ++band) {
            QCOMPARE(presets[0].settings.bandsDb[band], 0.0);
        }
        QCOMPARE(presets[0].settings.preampDb, -12.0);
        QCOMPARE(Audio::WriteEqf(presets).mid(31 + 257, 10), QByteArray(10, char(31)));
    }

    void eqfKeepsNonLatinNames() {
        Audio::EqPreset preset;
        preset.name = QStringLiteral("Мой пресет");
        const QList<Audio::EqPreset> presets = Audio::ParseEqf(Audio::WriteEqf({preset}));
        QCOMPARE(presets[0].name, preset.name);
    }
};

QTEST_MAIN(TestDsp)
#include "dsp_test.moc"
