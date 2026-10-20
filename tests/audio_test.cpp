#include "audio/audio_engine.h"
#include "core/player.h"
#include "core/track_source.h"

#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QIODevice>
#include <QLatin1String>
#include <QMetaEnum>
#include <QObject>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <algorithm>
#include <functional>

using Audio::AudioEngine;

namespace {

QByteArray BareMp3Frames(const QByteArray& file) {
    const auto byte = [&](qsizetype index) { return static_cast<uchar>(file.at(index)); };
    qsizetype offset = 0;
    if (file.startsWith("ID3")) {
        const qsizetype tagSize =
            (byte(6) & 0x7f) << 21 | (byte(7) & 0x7f) << 14 | (byte(8) & 0x7f) << 7 | (byte(9) & 0x7f);
        const qsizetype footer = (byte(5) & 0x10) ? 10 : 0;
        offset = 10 + tagSize + footer;
    }
    // MPEG-1 Layer III: 144 * bitrate / rate bytes, one more with the padding bit.
    constexpr int kKbps[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    constexpr int kRates[] = {44'100, 48'000, 32'000, 0};
    const uchar flags = byte(offset + 2);
    const int rate = kRates[(flags >> 2) & 3];
    const qsizetype frameSize = rate ? 144 * kKbps[flags >> 4] * 1000 / rate + ((flags >> 1) & 1) : 0;
    const QByteArray firstFrame = file.mid(offset, frameSize);
    if (firstFrame.contains("Info") || firstFrame.contains("Xing")) {
        offset += frameSize;
    }
    return file.mid(offset);
}

QByteArray Id3Prefix(int paddingBytes) {
    QByteArray tag("ID3\x04\x00\x00", 6);
    for (const int shift : {21, 14, 7, 0}) {
        tag += static_cast<char>((paddingBytes >> shift) & 0x7f);
    }
    return tag + QByteArray(paddingBytes, '\0');
}

}  // namespace

class TestAudio : public QObject {
    Q_OBJECT
private:
    Audio::AudioEngine engine;
    QByteArray mp3;

    static QByteArray Fixture(const QString& name) {
        QFile file(QStringLiteral(QIYAA_TEST_DATA "/") + name);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    QString describeEngine(qsizetype advancedCount, qsizetype finishedCount = -1) const {
        return QStringLiteral("advanced %1, finished %2, at %3 s, %4, current %5, queued %6")
            .arg(advancedCount)
            .arg(finishedCount)
            .arg(engine.positionSeconds(), 0, 'f', 2)
            .arg(QLatin1String(
                QMetaEnum::fromType<Audio::AudioEngine::State>().valueToKey(static_cast<int>(engine.state()))
            ))
            .arg(engine.currentStream())
            .arg(engine.queuedStream());
    }

    void pumpUntil(const std::function<bool()>& condition, int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (!condition() && timer.elapsed() < timeoutMs) {
            engine.poll();
            QTest::qWait(20);
        }
    }
    void pumpFor(int ms) {
        pumpUntil([] { return false; }, ms);
    }

    Audio::AudioEngine::TStreamId startNearEnd(double at = 2.2) {
        const auto stream = engine.beginStream();
        engine.appendData(stream, mp3);
        engine.finishData(stream);
        pumpUntil([&] { return engine.state() == Audio::AudioEngine::State::Playing; }, 3000);
        engine.seek(at);
        pumpUntil([&] { return engine.positionSeconds() >= at; }, 2000);
        return stream;
    }

    QByteArray repeatedMp3(int times) const {
        const QByteArray frames = BareMp3Frames(mp3);
        QByteArray stream;
        for (int i = 0; i < times; ++i) {
            stream += frames;
        }
        return stream;
    }

    Audio::AudioEngine::TStreamId queueWhole(const QByteArray& data) {
        const auto stream = engine.queueStream();
        engine.appendData(stream, data);
        engine.finishData(stream);
        return stream;
    }

private Q_SLOTS:
    void initTestCase() {
        if (const Audio::AudioEngine::InitResult initResult = engine.init(); !initResult.ok) {
            QSKIP(qPrintable("no audio output: " + initResult.message));
        }
        engine.setVolume(0);
        QFile file(QStringLiteral(QIYAA_TEST_DATA "/sine440_3s.mp3"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        mp3 = file.readAll();
        qInfo("backend: %s", qPrintable(engine.backendName()));
    }

    void streamsInChunksAndPlays() {
        QSignalSpy finished(&engine, &Audio::AudioEngine::trackFinished);
        engine.beginStream();
        QCOMPARE(engine.state(), Audio::AudioEngine::State::Buffering);

        for (qsizetype offset = 0; offset < mp3.size(); offset += 4096) {
            engine.appendData(mp3.mid(offset, 4096));
            engine.poll();
            QTest::qWait(10);
        }
        engine.finishData();

        pumpUntil([&] { return engine.state() == Audio::AudioEngine::State::Playing; }, 3000);
        QCOMPARE(engine.state(), Audio::AudioEngine::State::Playing);
        QCOMPARE(engine.sourceSampleRate(), 44'100);
        QCOMPARE(engine.sourceChannels(), 2);

        const double startPosition = engine.positionSeconds();
        QTest::qWait(500);
        QVERIFY2(engine.positionSeconds() > startPosition + 0.2, "position does not advance");

        QVERIFY(engine.seek(2.5));
        pumpUntil([&] { return finished.count() > 0; }, 4000);
        QCOMPARE(finished.count(), 1);
        QVERIFY(engine.positionSeconds() >= 2.5);
    }

    void anMp4PlaysBeforeItHasFullyArrived() {
        QFile file(QStringLiteral(QIYAA_TEST_DATA "/sine440_3s_heaac.m4a"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray m4a = file.readAll();
        QSignalSpy finished(&engine, &Audio::AudioEngine::trackFinished);
        engine.beginStream();
        const qsizetype half = m4a.size() / 2;
        for (qsizetype offset = 0; offset < half; offset += 2048) {
            engine.appendData(m4a.mid(offset, std::min<qsizetype>(2048, half - offset)));
            engine.poll();
            QTest::qWait(10);
        }
        pumpUntil([&] { return engine.state() == Audio::AudioEngine::State::Playing; }, 3000);
        QCOMPARE(engine.state(), Audio::AudioEngine::State::Playing);
        QCOMPARE(engine.sourceSampleRate(), 44'100);
        QCOMPARE(engine.sourceChannels(), 2);
        engine.appendData(m4a.mid(half));
        engine.finishData();
        QVERIFY(engine.seek(2.5));
        pumpUntil([&] { return finished.count() > 0; }, 4000);
        QCOMPARE(finished.count(), 1);
        QVERIFY(engine.positionSeconds() >= 2.5);
    }

    void pauseStopsTheClock() {
        engine.beginStream();
        engine.appendData(mp3);
        engine.finishData();
        pumpUntil([&] { return engine.state() == Audio::AudioEngine::State::Playing; }, 3000);
        engine.pause();
        QCOMPARE(engine.state(), Audio::AudioEngine::State::Paused);
        const double pausedPosition = engine.positionSeconds();
        QTest::qWait(300);
        QCOMPARE(engine.positionSeconds(), pausedPosition);
        engine.resume();
        QCOMPARE(engine.state(), Audio::AudioEngine::State::Playing);
        engine.stop();
        QCOMPARE(engine.state(), Audio::AudioEngine::State::Stopped);
    }

    void garbageReportsError() {
        QSignalSpy errors(&engine, &Audio::AudioEngine::errorOccurred);
        engine.beginStream();
        engine.appendData(QByteArray(20'000, 'x'));
        engine.finishData();
        pumpUntil([&] { return errors.count() > 0; }, 3000);
        QCOMPARE(errors.count(), 1);
        const QString message = errors.first().first().toString();
        QVERIFY2(message.contains(QStringLiteral("20000 bytes")), qPrintable(message));
        QCOMPARE(engine.state(), Audio::AudioEngine::State::Stopped);
    }

    void rapidSeeksWhileDownloadingLandOnTheLastTarget() {
        const QByteArray longMp3 = repeatedMp3(13);
        engine.beginStream();
        qsizetype fed = 0;
        auto feed = [&](qsizetype byteCount) {
            engine.appendData(longMp3.mid(fed, byteCount));
            fed += byteCount;
        };
        feed(64 * 1024);
        pumpUntil([&] { return engine.state() == Audio::AudioEngine::State::Playing; }, 3000);
        QCOMPARE(engine.state(), Audio::AudioEngine::State::Playing);
        const double targets[] = {1.0, 30.0, 2.0, 35.0, 0.5, 20.0, 3.0, 10.0};
        for (int round = 0; round < 3; ++round) {
            for (double target : targets) {
                QVERIFY(engine.seek(target));
                QTest::qWait(5);
                if (fed < longMp3.size()) {
                    feed(16 * 1024);
                }
                engine.poll();
            }
        }
        feed(longMp3.size() - fed);
        engine.finishData();
        QVERIFY(engine.seek(12.0));
        pumpUntil([&] { return engine.positionSeconds() > 12.2; }, 3000);
        const double position = engine.positionSeconds();
        QVERIFY2(position >= 12.0 && position < 14.0, qPrintable(QString::number(position)));
        engine.stop();
    }

    void seekingIntoTheDownloadedPartPlaysAtOnceWhileTheDownloadStalls() {  // TR-09
        const QByteArray longMp3 = repeatedMp3(4);
        engine.beginStream();
        engine.appendData(longMp3.left(longMp3.size() / 2));
        pumpUntil([&] { return engine.state() == Audio::AudioEngine::State::Playing; }, 3000);
        QCOMPARE(engine.state(), Audio::AudioEngine::State::Playing);
        pumpUntil([&] { return engine.positionSeconds() >= 2.5; }, 4000);
        QVERIFY2(engine.positionSeconds() >= 2.5, qPrintable(QString::number(engine.positionSeconds())));
        QVERIFY(engine.seek(3.0));
        pumpFor(300);
        const double early = engine.positionSeconds();
        pumpFor(300);
        const double later = engine.positionSeconds();
        engine.stop();
        QVERIFY2(
            later > early + 0.15 && later < 4.0,
            qPrintable(QStringLiteral("%1 s, then %2 s").arg(early, 0, 'f', 2).arg(later, 0, 'f', 2))
        );
    }

    void positionShowsTheTargetThroughASeriesOfSeeks() {  // TR-08
        engine.beginStream();
        engine.appendData(repeatedMp3(4));
        engine.finishData();
        pumpUntil([&] { return engine.state() == Audio::AudioEngine::State::Playing; }, 3000);
        QElapsedTimer sinceSeek;
        for (const double target : {8.0, 2.0, 10.5, 0.5, 6.0}) {
            QVERIFY(engine.seek(target));
            sinceSeek.start();
            double shown = target;
            while (sinceSeek.elapsed() < 300 || (shown <= target && sinceSeek.elapsed() < 3000)) {
                const double position = engine.positionSeconds();
                const double latest = target + static_cast<double>(sinceSeek.elapsed()) / 1000 + 0.3;
                QVERIFY2(
                    position >= shown && position <= latest,
                    qPrintable(QStringLiteral("seek to %1 s: %2 s after %3 s").arg(target).arg(position).arg(shown))
                );
                shown = position;
                engine.poll();
                QTest::qWait(5);
            }
            QVERIFY2(shown > target, qPrintable(QStringLiteral("seek to %1 s: no audio after it").arg(target)));
        }
        engine.stop();
    }

    void seekingWhilePausedPlaysAtOnceOnResume() {
        engine.beginStream();
        engine.appendData(repeatedMp3(2));
        engine.finishData();
        pumpUntil([&] { return engine.state() == Audio::AudioEngine::State::Playing; }, 3000);
        engine.pause();
        QVERIFY(engine.seek(4.0));
        QCOMPARE(engine.positionSeconds(), 4.0);
        pumpFor(300);
        QCOMPARE(engine.positionSeconds(), 4.0);
        QSignalSpy states(&engine, &Audio::AudioEngine::stateChanged);
        engine.resume();
        QCOMPARE(engine.state(), Audio::AudioEngine::State::Playing);
        pumpFor(300);
        QCOMPARE(states.count(), 1);
        const double position = engine.positionSeconds();
        QVERIFY2(position > 4.1 && position < 4.8, qPrintable(QString::number(position)));
        engine.stop();
    }

    void seekingAheadWhileTheDownloadStallsWaitsForIt() {
        QSignalSpy finished(&engine, &Audio::AudioEngine::trackFinished);
        const QByteArray longMp3 = repeatedMp3(4);
        engine.beginStream();
        engine.appendData(longMp3.left(longMp3.size() / 4));
        pumpUntil([&] { return engine.state() == Audio::AudioEngine::State::Playing; }, 3000);
        pumpFor(300);
        QVERIFY(engine.seek(7.0));
        pumpFor(300);
        QVERIFY(engine.seek(8.0));
        pumpFor(300);
        engine.appendData(longMp3.mid(longMp3.size() / 4));
        engine.finishData();
        pumpUntil([&] { return engine.positionSeconds() > 8.2 || !finished.isEmpty(); }, 3000);
        QCOMPARE(finished.count(), 0);
        const double position = engine.positionSeconds();
        QVERIFY2(position > 8.2 && position < 9.5, qPrintable(QString::number(position)));
        engine.stop();
    }

    void seekBeforeTheStreamOpensIsKept() {
        engine.beginStream();
        QVERIFY(engine.seek(1.5));
        QCOMPARE(engine.positionSeconds(), 1.5);
        engine.appendData(mp3);
        engine.finishData();
        pumpUntil([&] { return engine.state() == Audio::AudioEngine::State::Playing; }, 3000);
        pumpFor(200);
        const double position = engine.positionSeconds();
        QVERIFY2(position >= 1.5 && position < 2.3, qPrintable(QString::number(position)));
        engine.stop();
    }

    void flacAndWavPlayBeforeTheirDownloadEnds_data() {
        QTest::addColumn<QByteArray>("file");
        const QByteArray flac = Fixture(QStringLiteral("sine440_1s.flac"));
        QTest::newRow("flac") << flac;
        QTest::newRow("wav") << Fixture(QStringLiteral("sine440_1s.wav"));
        QTest::newRow("flac after ID3v2") << Id3Prefix(4096) + flac;
    }

    void flacAndWavPlayBeforeTheirDownloadEnds() {
        QFETCH(QByteArray, file);
        QVERIFY(file.size() > 20'000);
        engine.beginStream();
        engine.appendData(file.left(file.size() * 3 / 5));
        pumpUntil([&] { return engine.state() == Audio::AudioEngine::State::Playing; }, 3000);
        const Audio::AudioEngine::State state = engine.state();
        const int rate = engine.sourceSampleRate();
        const int channels = engine.sourceChannels();
        engine.stop();
        QCOMPARE(state, Audio::AudioEngine::State::Playing);
        QCOMPARE(rate, 22'050);
        QCOMPARE(channels, 1);
    }

    void playerPollsTheEngine() {
        Core::SourceRegistry sources;
        Core::Player player(&engine, &sources);
        QSignalSpy finished(&engine, &Audio::AudioEngine::trackFinished);
        engine.beginStream();
        engine.appendData(mp3);
        engine.finishData();
        QVERIFY(QTest::qWaitFor([&] { return engine.state() == Audio::AudioEngine::State::Playing; }, 3000));
        QVERIFY(engine.seek(2.7));
        QVERIFY(finished.wait(4000));
    }

    void restartingWhileStreamingDoesNotHang() {
        for (int i = 0; i < 5; ++i) {
            engine.beginStream();
            engine.appendData(mp3.left(8000));
            QTest::qWait(30);
        }
        engine.stop();
        QCOMPARE(engine.state(), Audio::AudioEngine::State::Stopped);
    }

    void queuedStreamFollowsWithoutGap() {
        QSignalSpy finished(&engine, &Audio::AudioEngine::trackFinished);
        QSignalSpy advanced(&engine, &Audio::AudioEngine::trackAdvanced);
        startNearEnd();
        QSignalSpy states(&engine, &Audio::AudioEngine::stateChanged);
        const auto nextStream = queueWhole(mp3);
        QVERIFY(nextStream != 0);
        QCOMPARE(engine.queuedStream(), nextStream);
        pumpUntil([&] { return advanced.count() > 0 || finished.count() > 0; }, 3000);
        QVERIFY2(advanced.count() == 1, qPrintable(describeEngine(advanced.count(), finished.count())));
        QCOMPARE(finished.count(), 0);
        QVERIFY(states.isEmpty());
        QCOMPARE(engine.currentStream(), nextStream);
        QCOMPARE(engine.queuedStream(), static_cast<Audio::AudioEngine::TStreamId>(0));
        QVERIFY2(engine.positionSeconds() < 0.3, qPrintable(QString::number(engine.positionSeconds())));
        QVERIFY(engine.seek(2.7));
        pumpUntil([&] { return finished.count() > 0; }, 3000);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(advanced.count(), 1);
        engine.stop();
    }

    void seekingBackUndoesTheChain() {
        QSignalSpy advanced(&engine, &Audio::AudioEngine::trackAdvanced);
        startNearEnd(2.3);
        const auto nextStream = queueWhole(mp3);
        QTest::qWait(150);  // the rest of track 1 is decoded and track 2 chained behind it
        QVERIFY(engine.seek(0.5));
        pumpUntil(
            [&] {
                const double position = engine.positionSeconds();
                return position >= 0.8 && position < 1.5;
            },
            2000
        );
        QCOMPARE(advanced.count(), 0);
        const double position = engine.positionSeconds();
        QVERIFY2(position >= 0.5 && position < 1.5, qPrintable(QString::number(position)));
        QCOMPARE(engine.queuedStream(), nextStream);
        QVERIFY(engine.seek(2.5));
        pumpUntil([&] { return advanced.count() > 0; }, 3000);
        QVERIFY2(advanced.count() == 1, qPrintable(describeEngine(advanced.count())));
        QVERIFY(engine.positionSeconds() < 0.3);
        engine.stop();
    }

    void clearingAChainedStreamStopsAtTheBoundary() {
        QSignalSpy finished(&engine, &Audio::AudioEngine::trackFinished);
        QSignalSpy advanced(&engine, &Audio::AudioEngine::trackAdvanced);
        startNearEnd(2.3);
        queueWhole(mp3);
        QTest::qWait(150);  // chained
        engine.clearQueued();
        pumpUntil([&] { return finished.count() > 0 || advanced.count() > 0; }, 3000);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(advanced.count(), 0);
        engine.stop();
    }

    void clearingAQueuedStreamBeforeItChains() {
        QSignalSpy finished(&engine, &Audio::AudioEngine::trackFinished);
        QSignalSpy advanced(&engine, &Audio::AudioEngine::trackAdvanced);
        startNearEnd(2.0);
        const auto nextStream = engine.queueStream();
        engine.appendData(nextStream, mp3.left(4000));
        engine.clearQueued();
        engine.appendData(nextStream, mp3.mid(4000));
        engine.finishData(nextStream);
        pumpUntil([&] { return finished.count() > 0 || advanced.count() > 0; }, 3000);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(advanced.count(), 0);
        QCOMPARE(engine.queuedStream(), static_cast<Audio::AudioEngine::TStreamId>(0));
        engine.stop();
    }

    void clearingAStreamTheCallbackAlreadyPlays() {
        QSignalSpy finished(&engine, &Audio::AudioEngine::trackFinished);
        QSignalSpy advanced(&engine, &Audio::AudioEngine::trackAdvanced);
        startNearEnd(2.6);
        queueWhole(mp3);
        QTest::qWait(800);
        engine.clearQueued();
        pumpUntil([&] { return finished.count() > 0 || advanced.count() > 0; }, 3000);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(advanced.count(), 0);
        pumpFor(150);
        const uint32_t cursor = engine.visCursor();
        const double position = engine.positionSeconds();
        pumpFor(300);
        QCOMPARE(engine.visCursor(), cursor);
        QCOMPARE(engine.positionSeconds(), position);
        engine.stop();
    }

    void playQueuedNowJumpsImmediately() {
        startNearEnd(0.5);
        const auto nextStream = queueWhole(mp3);
        QCOMPARE(engine.playQueuedNow(), nextStream);
        QCOMPARE(engine.currentStream(), nextStream);
        pumpUntil([&] { return engine.state() == Audio::AudioEngine::State::Playing; }, 3000);
        QVERIFY(engine.positionSeconds() < 0.5);
        QCOMPARE(engine.queuedStream(), static_cast<Audio::AudioEngine::TStreamId>(0));
        engine.stop();
    }

    void undecodableQueuedStreamIsSkipped() {
        QSignalSpy finished(&engine, &Audio::AudioEngine::trackFinished);
        QSignalSpy advanced(&engine, &Audio::AudioEngine::trackAdvanced);
        startNearEnd(2.3);
        queueWhole(QByteArray(100'000, 'x'));
        pumpUntil([&] { return finished.count() > 0 || advanced.count() > 0; }, 3000);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(advanced.count(), 0);
        QCOMPARE(engine.queuedStream(), static_cast<Audio::AudioEngine::TStreamId>(0));
        engine.stop();
    }

    void queuedStreamArrivingLateStillChains() {
        QSignalSpy finished(&engine, &Audio::AudioEngine::trackFinished);
        QSignalSpy advanced(&engine, &Audio::AudioEngine::trackAdvanced);
        startNearEnd(2.0);
        const auto nextStream = engine.queueStream();
        engine.appendData(nextStream, mp3.left(4000));  // not enough to start decoding it yet
        QTest::qWait(200);
        engine.appendData(nextStream, mp3.mid(4000));
        engine.finishData(nextStream);
        pumpUntil([&] { return finished.count() > 0 || advanced.count() > 0; }, 3000);
        QVERIFY2(advanced.count() == 1, qPrintable(describeEngine(advanced.count(), finished.count())));
        QCOMPARE(finished.count(), 0);
        engine.stop();
    }

    void partlyDownloadedQueuedStreamIsNotChained() {
        QSignalSpy finished(&engine, &Audio::AudioEngine::trackFinished);
        QSignalSpy advanced(&engine, &Audio::AudioEngine::trackAdvanced);
        startNearEnd(2.4);
        const auto nextStream = engine.queueStream();
        engine.appendData(nextStream, mp3.left(mp3.size() / 2));
        pumpUntil([&] { return finished.count() > 0 || advanced.count() > 0; }, 3000);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(advanced.count(), 0);
        QCOMPARE(engine.playQueuedNow(), nextStream);
        engine.appendData(nextStream, mp3.mid(mp3.size() / 2));
        engine.finishData(nextStream);
        pumpUntil([&] { return engine.state() == Audio::AudioEngine::State::Playing; }, 3000);
        QCOMPARE(engine.state(), Audio::AudioEngine::State::Playing);
        engine.stop();
    }

    void clearingAChainedStreamThenRestartingDoesNotHang() {
        for (int round = 0; round < 2; ++round) {
            startNearEnd(2.3);
            queueWhole(mp3);
            QTest::qWait(150);  // chained
            engine.clearQueued();
            QElapsedTimer timer;
            timer.start();
            if (round == 0) {
                engine.stop();
            } else {
                engine.beginStream();
            }
            QVERIFY2(timer.elapsed() < 1000, qPrintable(QString::number(timer.elapsed())));
        }
        engine.stop();
    }

    void stopReturnsPromptlyWhileQueuedDataIsMissing() {
        startNearEnd(2.9);
        const auto nextStream = engine.queueStream();
        engine.appendData(nextStream, mp3.left(70'000 < mp3.size() ? 70'000 : mp3.size() / 2));
        QTest::qWait(300);
        QElapsedTimer timer;
        timer.start();
        engine.stop();
        QVERIFY(timer.elapsed() < 1000);
        QCOMPARE(engine.state(), Audio::AudioEngine::State::Stopped);
    }

    void randomCallsNeitherHangNorReportUnknownStreams() {
        using TStreamId = Audio::AudioEngine::TStreamId;
        const QByteArray garbage(30'000, 'x');
        for (const quint32 seed : {1u, 2u, 3u}) {
            QRandomGenerator random(seed);
            QHash<TStreamId, qsizetype> fed;
            QHash<TStreamId, bool> broken;
            auto feed = [&](TStreamId stream) {
                if (!stream) {
                    return;
                }
                const QByteArray& file = broken.value(stream) ? garbage : mp3;
                const qsizetype offset = fed.value(stream);
                const qsizetype size = 2000 + random.bounded(8000);
                engine.appendData(stream, file.mid(offset, size));
                fed[stream] = offset + size;
                if (offset + size >= file.size()) {
                    engine.finishData(stream);
                }
            };
            auto start = [&](TStreamId stream) {
                fed[stream] = 0;
                broken[stream] = random.bounded(10) == 0;
            };
            for (int step = 0; step < 250; ++step) {
                const QString where = QStringLiteral("seed %1, step %2").arg(seed).arg(step);
                QElapsedTimer timer;
                timer.start();
                switch (random.bounded(12)) {
                    case 0: start(engine.beginStream(random.bounded(2.0))); break;
                    case 1:
                        if (const TStreamId stream = engine.queueStream()) {
                            start(stream);
                        }
                        break;
                    case 2: engine.clearQueued(); break;
                    case 3: engine.playQueuedNow(); break;
                    case 4:
                    case 5: feed(engine.currentStream()); break;
                    case 6: feed(engine.queuedStream()); break;
                    case 7: engine.seek(random.bounded(3.2)); break;
                    case 8: engine.pause(); break;
                    case 9: engine.resume(); break;
                    case 10:
                        if (random.bounded(4) == 0) {
                            engine.stop();
                        }
                        break;
                    default: QTest::qWait(random.bounded(40)); break;
                }
                QVERIFY2(timer.elapsed() < 200, qPrintable(where));

                const TStreamId current = engine.currentStream();
                const TStreamId queued = engine.queuedStream();
                QSignalSpy advanced(&engine, &Audio::AudioEngine::trackAdvanced);
                QSignalSpy undecodable(&engine, &Audio::AudioEngine::streamUndecodable);
                engine.poll();
                if (!advanced.isEmpty()) {
                    QVERIFY2(queued && engine.currentStream() == queued, qPrintable(where));
                }
                if (!undecodable.isEmpty()) {
                    QVERIFY2(current && undecodable.first().first().toULongLong() == current, qPrintable(where));
                }
                QVERIFY2(engine.positionSeconds() >= 0, qPrintable(where));
            }
            engine.stop();
        }
    }
};

QTEST_GUILESS_MAIN(TestAudio)
#include "audio_test.moc"
