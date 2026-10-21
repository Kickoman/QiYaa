#include "audio/ogg_vorbis_backend.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QList>
#include <QObject>
#include <QString>
#include <QTest>
#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr ma_uint32 kRate = 44'100;

class MemoryDecoder {
public:
    explicit MemoryDecoder(const QByteArray& bytes)
        : data(bytes) {
        ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, 0);
        backends[0] = Audio::OggVorbisBackend();
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

    bool openedByOggBackend() const { return opened && decoder.pBackendVTable == Audio::OggVorbisBackend(); }
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

QByteArray ReadData(const char* name) {
    QFile file(QStringLiteral(QIYAA_TEST_DATA "/") + QString::fromLatin1(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QList<ma_uint64> PageGranules(const QByteArray& bytes) {
    QList<ma_uint64> granules;
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.constData());
    const auto size = static_cast<size_t>(bytes.size());
    for (size_t offset = 0; offset + 27 <= size;) {
        int64_t granule = 0;
        std::memcpy(&granule, data + offset + 6, sizeof(granule));
        const size_t segments = data[offset + 26];
        size_t body = 0;
        for (size_t i = 0; i < segments && offset + 27 + i < size; ++i) {
            body += data[offset + 27 + i];
        }
        if (granule > 0) {
            granules << static_cast<ma_uint64>(granule);
        }
        offset += 27 + segments + body;
    }
    return granules;
}

double RootMeanSquare(const std::vector<float>& samples, size_t first, size_t count) {
    double sum = 0;
    for (size_t i = first; i < first + count; ++i) {
        sum += static_cast<double>(samples[i]) * samples[i];
    }
    return std::sqrt(sum / static_cast<double>(count));
}

double LeftFrequency(const std::vector<float>& samples, size_t first, size_t frames) {
    int crossings = 0;
    for (size_t frame = first + 1; frame < first + frames; ++frame) {
        if (samples[(frame - 1) * 2] < 0 && samples[frame * 2] >= 0) {
            ++crossings;
        }
    }
    return crossings * static_cast<double>(kRate) / static_cast<double>(frames);
}

float LargestDifference(const std::vector<float>& part, const std::vector<float>& whole, ma_uint64 target) {
    float largest = 0;
    for (size_t i = 0; i < part.size(); ++i) {
        largest = std::max(largest, std::abs(part[i] - whole[target * 2 + i]));
    }
    return largest;
}

}  // namespace

class TestOggVorbis : public QObject {
    Q_OBJECT
private:
    QByteArray sine;
    QByteArray chirp;

private Q_SLOTS:
    void initTestCase() {
        sine = ReadData("sine440_3s.ogg");
        chirp = ReadData("chirp_12s.ogg");
        QVERIFY(!sine.isEmpty());
        QVERIFY(!chirp.isEmpty());
    }

    void recognisesOggByItsCapturePattern() {
        QVERIFY(Audio::IsOgg(sine.constData(), static_cast<size_t>(sine.size())));
        QVERIFY(!Audio::IsOgg("fLaC", 4));
        QVERIFY(!Audio::IsOgg("Ogg", 3));
    }

    void decodesVorbisAsA440HzSineAt44100Hz() {
        MemoryDecoder decoder(sine);
        QVERIFY(decoder.openedByOggBackend());
        QCOMPARE(decoder.sampleRate(), kRate);
        QCOMPARE(decoder.length(), ma_uint64{3 * kRate});

        const std::vector<float> samples = decoder.read(4 * kRate);
        QCOMPARE(samples.size() / 2, size_t{3 * kRate});
        const double level = RootMeanSquare(samples, kRate * 2, kRate * 2);
        QVERIFY2(level > 0.05 && level < 0.07, qPrintable(QString::number(level)));
        const double frequency = LeftFrequency(samples, kRate, kRate);
        QVERIFY2(std::abs(frequency - 440) < 3, qPrintable(QString::number(frequency)));
    }

    void endsAtTheLastPagesGranulePosition() {
        MemoryDecoder decoder(chirp);
        QVERIFY(decoder.openedByOggBackend());
        const QList<ma_uint64> granules = PageGranules(chirp);
        QVERIFY(!granules.isEmpty());
        QCOMPARE(decoder.length(), granules.last());
        QCOMPARE(decoder.read(granules.last() + 1000).size() / 2, granules.last());
    }

    void seekingLandsOnTheSameSamplesAsPlayingThrough() {
        MemoryDecoder decoder(sine);
        QVERIFY(decoder.openedByOggBackend());
        const std::vector<float> whole = decoder.read(decoder.length());
        for (const ma_uint64 target : {ma_uint64{66'150}, ma_uint64{2048}, ma_uint64{100}, ma_uint64{0}}) {
            QVERIFY(decoder.seek(target));
            const std::vector<float> part = decoder.read(4096);
            QCOMPARE(part.size(), size_t{4096 * 2});
            const float largest = LargestDifference(part, whole, target);
            QVERIFY2(largest < 1e-6f, qPrintable(QStringLiteral("at %1: %2").arg(target).arg(largest)));
        }
    }

    void seekingToEveryPageLandsExactly() {
        MemoryDecoder decoder(chirp);
        QVERIFY(decoder.openedByOggBackend());
        const ma_uint64 length = decoder.length();
        const std::vector<float> whole = decoder.read(length);
        QCOMPARE(whole.size() / 2, length);
        QList<ma_uint64> targets;
        for (const ma_uint64 granule : PageGranules(chirp)) {
            targets << granule - 1 << granule << granule + 1 << granule + 777;
        }
        std::reverse(targets.begin(), targets.end());
        for (const ma_uint64 target : targets) {
            if (target >= length) {
                continue;
            }
            QVERIFY(decoder.seek(target));
            const ma_uint64 frames = std::min<ma_uint64>(2048, length - target);
            const std::vector<float> part = decoder.read(frames);
            QCOMPARE(part.size(), size_t(frames * 2));
            const float largest = LargestDifference(part, whole, target);
            QVERIFY2(largest < 1e-6f, qPrintable(QStringLiteral("at %1: %2").arg(target).arg(largest)));
        }
    }

    void seekingPastTheEndReadsNothing() {
        MemoryDecoder decoder(sine);
        QVERIFY(decoder.openedByOggBackend());
        QVERIFY(decoder.seek(decoder.length()));
        QVERIFY(decoder.read(1024).empty());
    }

    void aFileCutShortPlaysWhatItHas() {
        MemoryDecoder decoder(chirp.left(chirp.size() / 2));
        QVERIFY(decoder.openedByOggBackend());
        const std::vector<float> samples = decoder.read(PageGranules(chirp).last());
        QVERIFY(!samples.empty());
        QVERIFY(samples.size() / 2 < PageGranules(chirp).last());
    }

    void anOggOfAnotherCodecIsRefused() {
        MemoryDecoder decoder(ReadData("sine440_1s_flac.ogg"));
        QVERIFY(!decoder.openedByOggBackend());
    }

    void decodesAGivenSample() {
        const QString path = qEnvironmentVariable("QIYAA_OGG_SAMPLE");
        if (path.isEmpty()) {
            QSKIP("QIYAA_OGG_SAMPLE is not set");
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        MemoryDecoder decoder(bytes);
        QVERIFY(decoder.openedByOggBackend());
        const ma_uint64 length = decoder.length();
        const std::vector<float> samples = decoder.read(length);
        qInfo(
            "%s: %u Hz, %.3f s, %.3f s decoded",
            qPrintable(path),
            decoder.sampleRate(),
            static_cast<double>(length) / decoder.sampleRate(),
            static_cast<double>(samples.size() / 2) / decoder.sampleRate()
        );
        QCOMPARE(samples.size() / 2, length);
        for (const ma_uint64 target : {length / 2 + 333, length / 3, length - 5000, ma_uint64{1000}}) {
            QVERIFY(decoder.seek(target));
            const std::vector<float> part = decoder.read(2048);
            const float largest = LargestDifference(part, samples, target);
            QVERIFY2(largest < 1e-6f, qPrintable(QStringLiteral("at %1: %2").arg(target).arg(largest)));
        }
    }
};

QTEST_GUILESS_MAIN(TestOggVorbis)
#include "ogg_vorbis_test.moc"
