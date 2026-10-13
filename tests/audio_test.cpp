#include "audio/audio_engine.h"
#include "core/player.h"
#include "yandex/api_client.h"
#include "yandex/library.h"

#include <QElapsedTimer>
#include <QFile>
#include <QIODevice>
#include <QLatin1String>
#include <QMetaEnum>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <algorithm>
#include <functional>

using Audio::AudioEngine;

namespace {

// The test mp3 without its ID3 tag and its LAME "Info" frame: copies of what is left join into one
// longer stream. With the Info frame first, the mp3 decoder stops after the length it states (3 s).
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

}  // namespace

class TestAudio : public QObject {
    Q_OBJECT
private:
    Audio::AudioEngine engine;
    QByteArray mp3;

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

    // One stream of `times` × 3 s.
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
        const QByteArray longMp3 = repeatedMp3(13);  // 39 s
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
        // 6 s of a 12 s stream, and then the download stalls. While more may come, the mp3 decoder
        // keeps 16 KB in reserve (2 s of this 64 kbps file), so about 4 s can be decoded: by 2.5 s the
        // decoder has used them all and waits for bytes.
        engine.appendData(longMp3.left(longMp3.size() / 2));
        pumpUntil([&] { return engine.state() == Audio::AudioEngine::State::Playing; }, 3000);
        QCOMPARE(engine.state(), Audio::AudioEngine::State::Playing);
        pumpUntil([&] { return engine.positionSeconds() >= 2.5; }, 4000);
        QVERIFY2(engine.positionSeconds() >= 2.5, qPrintable(QString::number(engine.positionSeconds())));
        QVERIFY(engine.seek(1.0));
        pumpFor(300);
        const double early = engine.positionSeconds();
        pumpFor(300);
        const double later = engine.positionSeconds();
        engine.stop();
        QEXPECT_FAIL("", "#24: the decoder serves the seek only after the next bytes arrive", Abort);
        QVERIFY2(
            later > early + 0.15 && later < 2.0,
            qPrintable(QStringLiteral("%1 s, then %2 s").arg(early, 0, 'f', 2).arg(later, 0, 'f', 2))
        );
    }

    void playerPollsTheEngine() {
        Yandex::ApiClient api(nullptr);
        Yandex::Library library(&api);
        Core::Player player(&library, &engine);
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
};

QTEST_GUILESS_MAIN(TestAudio)
#include "audio_test.moc"
