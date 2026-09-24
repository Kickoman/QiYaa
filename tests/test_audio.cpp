// Streams a small mp3 through the real engine. On machines without a sound card
// miniaudio falls back to its "Null" backend, which still runs in real time.
#include <QElapsedTimer>
#include <QFile>
#include <functional>
#include <QSignalSpy>
#include <QTest>

#include "audio/AudioEngine.h"
#include "core/Player.h"

using qiyaa::audio::AudioEngine;

class TestAudio : public QObject {
    Q_OBJECT
private:
    AudioEngine engine;
    QByteArray mp3;

    void pumpUntil(const std::function<bool()>& cond, int timeoutMs) {
        QElapsedTimer t;
        t.start();
        while (!cond() && t.elapsed() < timeoutMs) {
            engine.poll();
            QTest::qWait(20);
        }
    }

private Q_SLOTS:
    void initTestCase() {
        QString err;
        if (!engine.init(&err)) QSKIP(qPrintable("no audio output: " + err));
        engine.setVolume(0);  // silence, in case this runs on a real device
        QFile f(QStringLiteral(QIYAA_TEST_DATA "/sine440_3s.mp3"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        mp3 = f.readAll();
        qInfo("backend: %s", qPrintable(engine.backendName()));
    }

    void streamsInChunksAndPlays() {
        QSignalSpy finished(&engine, &AudioEngine::trackFinished);
        engine.beginStream();
        QCOMPARE(engine.state(), AudioEngine::State::Buffering);

        // Feed like a slow network: 4 KB every 10 ms.
        for (qsizetype off = 0; off < mp3.size(); off += 4096) {
            engine.appendData(mp3.mid(off, 4096));
            engine.poll();
            QTest::qWait(10);
        }
        engine.finishData();

        pumpUntil([&] { return engine.state() == AudioEngine::State::Playing; }, 3000);
        QCOMPARE(engine.state(), AudioEngine::State::Playing);
        QCOMPARE(engine.sourceSampleRate(), 44100);
        QCOMPARE(engine.sourceChannels(), 2);

        const double p0 = engine.positionSeconds();
        QTest::qWait(500);
        QVERIFY2(engine.positionSeconds() > p0 + 0.2, "position does not advance");

        // Seek near the end and wait for the end of the track.
        QVERIFY(engine.seek(2.5));
        pumpUntil([&] { return finished.count() > 0; }, 4000);
        QCOMPARE(finished.count(), 1);
        QVERIFY(engine.positionSeconds() >= 2.5);
    }

    void pauseStopsTheClock() {
        engine.beginStream();
        engine.appendData(mp3);
        engine.finishData();
        pumpUntil([&] { return engine.state() == AudioEngine::State::Playing; }, 3000);
        engine.pause();
        QCOMPARE(engine.state(), AudioEngine::State::Paused);
        const double p = engine.positionSeconds();
        QTest::qWait(300);
        QCOMPARE(engine.positionSeconds(), p);
        engine.resume();
        QCOMPARE(engine.state(), AudioEngine::State::Playing);
        engine.stop();
        QCOMPARE(engine.state(), AudioEngine::State::Stopped);
    }

    void garbageReportsError() {
        QSignalSpy errors(&engine, &AudioEngine::errorOccurred);
        engine.beginStream();
        engine.appendData(QByteArray(20000, 'x'));
        engine.finishData();
        pumpUntil([&] { return errors.count() > 0; }, 3000);
        QCOMPARE(errors.count(), 1);
        QCOMPARE(engine.state(), AudioEngine::State::Stopped);
    }

    void rapidSeeksWhileDownloading() {
        // ~39 s stream (the 3 s file repeated; MP3 frames concatenate), fed slowly,
        // with seeks both inside and ahead of what's downloaded.
        QByteArray longMp3;
        for (int i = 0; i < 13; ++i) longMp3 += mp3;
        engine.beginStream();
        qsizetype fed = 0;
        auto feed = [&](qsizetype n) {
            engine.appendData(longMp3.mid(fed, n));
            fed += n;
        };
        feed(64 * 1024);
        pumpUntil([&] { return engine.state() == AudioEngine::State::Playing; }, 3000);
        QCOMPARE(engine.state(), AudioEngine::State::Playing);
        const double targets[] = {1.0, 30.0, 2.0, 35.0, 0.5, 20.0, 3.0, 10.0};
        for (int round = 0; round < 3; ++round)
            for (double t : targets) {
                QVERIFY(engine.seek(t));
                QTest::qWait(5);
                if (fed < longMp3.size()) feed(16 * 1024);
                engine.poll();
            }
        feed(longMp3.size() - fed);
        engine.finishData();
        QVERIFY(engine.seek(12.0));
        pumpUntil([&] { return engine.positionSeconds() > 12.2; }, 3000);
        const double pos = engine.positionSeconds();
        QVERIFY2(pos >= 12.0 && pos < 14.0, qPrintable(QString::number(pos)));
        engine.stop();
    }

    void playerPollsTheEngine() {
        // No one calls engine.poll() here: the Player's own timer must notice the
        // end of the track (this used to depend on the main window's timer).
        qiyaa::yandex::ApiClient api(nullptr);
        qiyaa::yandex::Library lib(&api);
        qiyaa::Player player(&lib, &engine);
        QSignalSpy finished(&engine, &AudioEngine::trackFinished);
        engine.beginStream();
        engine.appendData(mp3);
        engine.finishData();
        QVERIFY(QTest::qWaitFor([&] { return engine.state() == AudioEngine::State::Playing; }, 3000));
        QVERIFY(engine.seek(2.7));
        QVERIFY(finished.wait(4000));
    }

    void restartWhileStreaming() {
        // Switching tracks mid-download must not hang or crash.
        for (int i = 0; i < 5; ++i) {
            engine.beginStream();
            engine.appendData(mp3.left(8000));
            QTest::qWait(30);
        }
        engine.stop();
        QCOMPARE(engine.state(), AudioEngine::State::Stopped);
    }
};

QTEST_GUILESS_MAIN(TestAudio)
#include "test_audio.moc"
