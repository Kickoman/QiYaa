// Equalizer filters, presets, FFT analyzer and visualizers.
#include "audio/eq_presets.h"
#include "audio/equalizer.h"
#include "skins/skin.h"
#include "ui/equalizer_window.h"
#include "vis/visualizer.h"

#include <QImage>
#include <QPainter>
#include <QTest>

#include <cmath>
#include <numbers>
#include <vector>

namespace {
std::vector<float> StereoSine(double hz, double rate, int frames, float amp = 0.5f) {
    std::vector<float> out(frames * 2);
    for (int i = 0; i < frames; ++i) {
        const float v = amp * float(std::sin(2 * std::numbers::pi * hz * i / rate));
        out[i * 2] = out[i * 2 + 1] = v;
    }
    return out;
}

double Rms(const std::vector<float>& v, int fromFrame) {
    double sum = 0;
    int n = 0;
    for (size_t i = fromFrame * 2; i < v.size(); i += 2, ++n) {
        sum += double(v[i]) * v[i];
    }
    return std::sqrt(sum / n);
}
}  // namespace

class TestDsp : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void flatIsTransparent() {
        Audio::EqSettings s;
        for (double hz : {30.0, 60.0, 1000.0, 10000.0, 18000.0}) {
            QVERIFY(std::abs(Audio::EqualizerDsp::ResponseDb(s, hz, 44100)) < 0.01);
        }
    }

    void bandBoostHitsItsFrequency() {
        Audio::EqSettings s;
        s.bandsDb[4] = 12;  // 1 kHz
        QVERIFY(std::abs(Audio::EqualizerDsp::ResponseDb(s, 1000, 44100) - 12) < 0.1);
        QVERIFY(std::abs(Audio::EqualizerDsp::ResponseDb(s, 60, 44100)) < 0.5);
        QVERIFY(std::abs(Audio::EqualizerDsp::ResponseDb(s, 16000, 44100)) < 0.5);
    }

    void preampAndDisable() {
        Audio::EqSettings s;
        s.preampDb = -6;
        QVERIFY(std::abs(Audio::EqualizerDsp::ResponseDb(s, 1000, 48000) + 6) < 0.01);
        s.enabled = false;
        QCOMPARE(Audio::EqualizerDsp::ResponseDb(s, 1000, 48000), 0.0);
    }

    void processingMatchesResponse() {
        Audio::EqualizerDsp eq;
        eq.setSampleRate(44100);
        Audio::EqSettings s;
        s.bandsDb[0] = -12;  // 60 Hz cut
        eq.publish(s);
        auto low = StereoSine(60, 44100, 44100);
        auto mid = StereoSine(3000, 44100, 44100);
        const double lowIn = Rms(low, 4410), midIn = Rms(mid, 4410);
        eq.process(low.data(), 44100);
        Audio::EqualizerDsp eq2;
        eq2.setSampleRate(44100);
        eq2.publish(s);
        eq2.process(mid.data(), 44100);
        const double lowDb = 20 * std::log10(Rms(low, 4410) / lowIn);
        const double midDb = 20 * std::log10(Rms(mid, 4410) / midIn);
        QVERIFY2(std::abs(lowDb + 12) < 0.5, qPrintable(QString::number(lowDb)));
        QVERIFY2(std::abs(midDb) < 0.3, qPrintable(QString::number(midDb)));
    }

    void settingsChangeWhileProcessing() {
        // New coefficients are picked up at the next block, no NaNs/blow-ups.
        Audio::EqualizerDsp eq;
        eq.setSampleRate(48000);
        auto buf = StereoSine(440, 48000, 48000);
        for (int block = 0; block < 100; ++block) {
            Audio::EqSettings s;
            s.bandsDb[block % 10] = (block % 2 ? 12 : -12);
            eq.publish(s);
            eq.process(buf.data() + (block * 480) * 2, 480);
        }
        for (float v : buf) {
            QVERIFY(std::isfinite(v) && std::abs(v) < 4.0f);
        }
    }

    void presetsLookRight() {
        const auto presets = Audio::BuiltinEqPresets();
        QCOMPARE(presets.size(), 17);
        const auto bass =
            std::find_if(presets.cbegin(), presets.cend(), [](const Audio::EqPreset& p) {
                return p.name == "Full Bass";
            });
        QVERIFY(bass != presets.cend());
        QVERIFY(bass->settings.bandsDb[0] > 5);  // boosts lows
        QVERIFY(bass->settings.bandsDb[9] < -5);  // cuts highs
        QCOMPARE(Audio::EqfToDb(1), -12.0);
        QCOMPARE(Audio::EqfToDb(64), 12.0);
    }

    void eqfRoundTrip() {
        Audio::EqPreset p;
        p.name = QStringLiteral("My EQ");
        for (int i = 0; i < Audio::kEqBands; ++i) {
            p.settings.bandsDb[i] = -12.0 + i * 2.6;
        }
        p.settings.preampDb = 3.0;
        const QByteArray file = Audio::WriteEqf({p, Audio::BuiltinEqPresets().first()});
        QCOMPARE(file.size(), 31 + 2 * 268);  // header + 2 presets (name 257 + 11 values)
        QVERIFY(file.startsWith("Winamp EQ library file v1.1\x1a!--"));
        QList<Audio::EqPreset> back;
        QVERIFY(Audio::ParseEqf(file, &back));
        QCOMPARE(back.size(), 2);
        QCOMPARE(back[0].name, QStringLiteral("My EQ"));
        for (int i = 0; i < Audio::kEqBands; ++i) {
            // 64 levels over 24 dB: half a step (0.19 dB) + rounding to 0.1 dB.
            QVERIFY2(
                std::abs(back[0].settings.bandsDb[i] - p.settings.bandsDb[i]) <= 0.25,
                qPrintable(QString::number(i))
            );
        }
        QVERIFY(std::abs(back[0].settings.preampDb - 3.0) <= 0.25);
        QCOMPARE(back[1].name, QStringLiteral("Classical"));
    }

    void eqfKnownBytes() {
        // Max +12 dB is stored as 0, min -12 dB as 63 (webamp's max/min sample files).
        QByteArray file("Winamp EQ library file v1.1\x1a!--");
        QByteArray name("max");
        name.append(QByteArray(257 - name.size(), '\0'));
        file += name + QByteArray(10, char(0)) + QByteArray(1, char(63));
        QList<Audio::EqPreset> p;
        QVERIFY(Audio::ParseEqf(file, &p));
        QCOMPARE(p[0].settings.bandsDb[0], 12.0);
        QCOMPARE(p[0].settings.preampDb, -12.0);
        QVERIFY(!Audio::ParseEqf("not an eqf", &p));
    }

    void graphSplinePassesThroughBands() {
        Audio::EqSettings s;
        s.bandsDb[3] = 12;
        const QList<double> ys = Ui::EqualizerWindow::GraphCurve(s);
        QCOMPARE(ys.size(), 9 * 12 + 1);
        QVERIFY(std::abs(ys[3 * 12] - 0) < 1e-6);  // +12 dB = top row
        QVERIFY(std::abs(ys[0] - 9) < 1e-6);  // 0 dB = middle
        QVERIFY(ys[30] < 9 && ys[42] < 9);  // smooth around the peak
    }

    void analyzerFindsTheTone() {
        Vis::Analyzer an(1024);
        const double rate = 44100, hz = 1000;
        std::vector<float> mono(1024);
        for (int i = 0; i < 1024; ++i) {
            mono[i] = float(std::sin(2 * std::numbers::pi * hz * i / rate));
        }
        const auto& db = an.analyze(mono);
        QCOMPARE(int(db.size()), 513);
        const int peak = int(std::max_element(db.begin(), db.end()) - db.begin());
        QCOMPARE(peak, int(std::lround(hz / (rate / 1024))));
        QVERIFY(std::abs(db[peak]) < 1.5);  // full-scale sine ~ 0 dBFS
        QVERIFY(db[400] < -50);
    }

    void spectrumAndScopeRender() {
        const Skins::Skin skin = Skins::Skin::BuiltinBase();
        Vis::Analyzer an(1024);
        std::vector<float> l(1024), r(1024), mono(1024);
        for (int i = 0; i < 1024; ++i) {
            l[i] = r[i] = mono[i] =
                0.8f * float(std::sin(2 * std::numbers::pi * 200 * i / 44100.0));
        }
        const auto& db = an.analyze(mono);
        Vis::VisFrame f{l, r, db, 44100, 1024};

        for (auto make : {Vis::MakeSpectrum, Vis::MakeOscilloscope}) {
            auto v = make();
            QImage img(76, 16, QImage::Format_ARGB32);
            img.fill(Qt::black);
            v->update(f);
            QPainter p(&img);
            v->render(p, QRect(0, 0, 76, 16), skin);
            p.end();
            int lit = 0;
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 76; ++x) {
                    lit += img.pixel(x, y) != qRgb(0, 0, 0);
                }
            }
            QVERIFY2(lit > 10, qPrintable(v->name()));
        }
    }

    void eqfCentreAndOutOfRangeBytes() {
        QByteArray f("Winamp EQ library file v1.1\x1a!--", 31);
        QByteArray name("Flat");
        name.append(QByteArray(257 - name.size(), '\0'));
        f += name;
        f += QByteArray(10, char(31));  // Winamp's own midline.EQF: 64 - 31 = 33
        f += char(255);  // garbage: must not reach the DSP as -85 dB
        QList<Audio::EqPreset> ps;
        QVERIFY(Audio::ParseEqf(f, &ps));
        for (int b = 0; b < Audio::kEqBands; ++b) {
            QCOMPARE(ps[0].settings.bandsDb[b], 0.0);
        }
        QCOMPARE(ps[0].settings.preampDb, -12.0);
        // And flat is written as 31 again.
        QCOMPARE(Audio::WriteEqf(ps).mid(31 + 257, 10), QByteArray(10, char(31)));
    }

    void eqfKeepsNonLatinNames() {
        Audio::EqPreset p;
        p.name = QStringLiteral("Мой пресет");
        QList<Audio::EqPreset> ps;
        QVERIFY(Audio::ParseEqf(Audio::WriteEqf({p}), &ps));
        QCOMPARE(ps[0].name, p.name);
    }
};

QTEST_MAIN(TestDsp)
#include "dsp_test.moc"
