#include "audio/mp4_aac_backend.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QObject>
#include <QString>
#include <QTest>
#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr ma_uint32 kRate = 44'100;

class MemoryDecoder {
public:
    explicit MemoryDecoder(const QByteArray& bytes)
        : data(bytes) {
        ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, 0);
        backends[0] = Audio::Mp4AacBackend();
        config.ppCustomBackendVTables = backends;
        config.customBackendCount = 1;
        opened =
            ma_decoder_init_memory(data.constData(), static_cast<size_t>(data.size()), &config, &decoder) == MA_SUCCESS;
    }
    ~MemoryDecoder() {
        if (opened) {
            ma_decoder_uninit(&decoder);
        }
    }
    MemoryDecoder(const MemoryDecoder&) = delete;
    MemoryDecoder& operator=(const MemoryDecoder&) = delete;

    bool openedByMp4Backend() const { return opened && decoder.pBackendVTable == Audio::Mp4AacBackend(); }
    ma_uint32 sampleRate() const { return decoder.outputSampleRate; }
    ma_uint64 length() {
        ma_uint64 frames = 0;
        ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
        return frames;
    }
    std::vector<float> read(ma_uint64 frames) {
        std::vector<float> samples(frames * 2);
        ma_uint64 framesRead = 0;
        ma_decoder_read_pcm_frames(&decoder, samples.data(), frames, &framesRead);
        samples.resize(framesRead * 2);
        return samples;
    }
    bool seek(ma_uint64 frame) { return ma_decoder_seek_to_pcm_frame(&decoder, frame) == MA_SUCCESS; }

private:
    QByteArray data;
    ma_decoding_backend_vtable* backends[1] = {};
    ma_decoder decoder{};
    bool opened = false;
};

double RootMeanSquare(const std::vector<float>& samples, size_t first, size_t count) {
    double sum = 0;
    for (size_t i = first; i < first + count; ++i) {
        sum += static_cast<double>(samples[i]) * samples[i];
    }
    return std::sqrt(sum / static_cast<double>(count));
}

// Rising zero crossings of the left channel per second, from frame `first` on.
double LeftFrequency(const std::vector<float>& samples, size_t first, size_t frames) {
    int crossings = 0;
    for (size_t frame = first + 1; frame < first + frames; ++frame) {
        if (samples[(frame - 1) * 2] < 0 && samples[frame * 2] >= 0) {
            ++crossings;
        }
    }
    return crossings * static_cast<double>(kRate) / static_cast<double>(frames);
}

}  // namespace

class TestMp4Aac : public QObject {
    Q_OBJECT
private:
    QByteArray m4a;

private Q_SLOTS:
    void initTestCase() {
        QFile file(QStringLiteral(QIYAA_TEST_DATA "/sine440_3s_heaac.m4a"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        m4a = file.readAll();
    }

    void recognisesMp4ByItsFtypBox() {
        QVERIFY(Audio::IsMp4(m4a.constData(), static_cast<size_t>(m4a.size())));
        QVERIFY(!Audio::IsMp4("ID3\x04\x00\x00\x00\x00", 8));
        QVERIFY(!Audio::IsMp4(
            "\x00\x00\x00\x1c"
            "ftyp",
            7
        ));
    }

    void decodesHeAacAsA440HzSineAt44100Hz() {
        MemoryDecoder decoder(m4a);
        QVERIFY(decoder.openedByMp4Backend());
        QCOMPARE(decoder.sampleRate(), kRate);
        const ma_uint64 length = decoder.length();
        QVERIFY2(length >= 3 * kRate && length <= 3.2 * kRate, qPrintable(QString::number(length)));

        const std::vector<float> samples = decoder.read(length + kRate);
        QCOMPARE(samples.size() / 2, length);
        const double level = RootMeanSquare(samples, kRate * 2, kRate * 2);
        QVERIFY2(level > 0.25 && level < 0.45, qPrintable(QString::number(level)));
        const double frequency = LeftFrequency(samples, kRate, kRate);
        QVERIFY2(std::abs(frequency - 440) < 3, qPrintable(QString::number(frequency)));
    }

    void seekingLandsOnTheSameSamplesAsPlayingThrough() {
        MemoryDecoder decoder(m4a);
        QVERIFY(decoder.openedByMp4Backend());
        const std::vector<float> whole = decoder.read(decoder.length());
        for (const ma_uint64 target : {ma_uint64{66'150}, ma_uint64{2048}, ma_uint64{100}, ma_uint64{0}}) {
            QVERIFY(decoder.seek(target));
            const std::vector<float> part = decoder.read(4096);
            QCOMPARE(part.size(), size_t{4096 * 2});
            float largest = 0;
            for (size_t i = 0; i < part.size(); ++i) {
                largest = std::max(largest, std::abs(part[i] - whole[target * 2 + i]));
            }
            QVERIFY2(largest < 0.005f, qPrintable(QStringLiteral("at %1: %2").arg(target).arg(largest)));
        }
    }

    void seekingPastTheEndReadsNothing() {
        MemoryDecoder decoder(m4a);
        QVERIFY(decoder.openedByMp4Backend());
        QVERIFY(decoder.seek(decoder.length()));
        QVERIFY(decoder.read(1024).empty());
    }

    void aFileCutShortPlaysWhatItHas() {
        MemoryDecoder decoder(m4a.left(m4a.size() / 2));
        QVERIFY(decoder.openedByMp4Backend());
        const ma_uint64 length = decoder.length();
        const std::vector<float> samples = decoder.read(length);
        QVERIFY(!samples.empty());
        QVERIFY(samples.size() / 2 < length);
    }

    void anMp4WithoutItsIndexIsRefused() {
        QByteArray broken = m4a.left(28);
        broken += QByteArray(4000, 'x');
        MemoryDecoder decoder(broken);
        QVERIFY(!decoder.openedByMp4Backend());
    }

    // A file kept outside the repository, such as a Yandex Music economy file:
    // QIYAA_MP4_SAMPLE=<path>.
    void decodesAGivenSample() {
        const QString path = qEnvironmentVariable("QIYAA_MP4_SAMPLE");
        if (path.isEmpty()) {
            QSKIP("QIYAA_MP4_SAMPLE is not set");
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        MemoryDecoder decoder(file.readAll());
        QVERIFY(decoder.openedByMp4Backend());
        const ma_uint64 length = decoder.length();
        const std::vector<float> samples = decoder.read(length);
        qInfo(
            "%s: %u Hz, %.1f s, %.1f s decoded",
            qPrintable(path),
            decoder.sampleRate(),
            static_cast<double>(length) / decoder.sampleRate(),
            static_cast<double>(samples.size() / 2) / decoder.sampleRate()
        );
        QCOMPARE(samples.size() / 2, length);
        QVERIFY(RootMeanSquare(samples, 0, samples.size()) > 0.01);
    }
};

QTEST_GUILESS_MAIN(TestMp4Aac)
#include "mp4_aac_test.moc"
