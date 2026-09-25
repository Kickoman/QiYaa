// Milkdrop: the preset collection (no OpenGL needed) and, where an OpenGL 3.3
// context can be made (CTest runs this under Xvfb when it can), projectM
// rendering inside the window.
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "audio/AudioEngine.h"
#include "skin/Skin.h"
#include "ui/MilkdropWindow.h"
#include "vis/MilkdropPresets.h"
#include "vis/MilkdropView.h"

using namespace qiyaa;

namespace {
// A tiny valid Milkdrop preset: a waveform and some zoom, no shaders.
QByteArray simplePreset(double zoom) {
    return QByteArray("[preset00]\nfDecay=0.98\nzoom=") + QByteArray::number(zoom) +
           "\nwave_r=1\nwave_g=0.5\nwave_b=0.2\nnWaveMode=2\nfWaveScale=1.5\n"
           "per_frame_1=wave_r = 0.5 + 0.5*sin(time);\n";
}
void writeFile(const QString& path, const QByteArray& data) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}
}  // namespace

class TestMilkdrop : public QObject {
    Q_OBJECT
private:
    QTemporaryDir builtIn, user;
    Skin skin = Skin::builtinBase();
    audio::AudioEngine engine;  // not initialised: silence

private Q_SLOTS:
    void initTestCase() {
        writeFile(builtIn.filePath("b-second.milk"), simplePreset(1.02));
        writeFile(builtIn.filePath("A-first.milk"), simplePreset(0.98));
        writeFile(builtIn.filePath("c-third.milk"), simplePreset(1.05));
        writeFile(builtIn.filePath("notes.txt"), "not a preset");
        writeFile(user.filePath("mine.milk"), simplePreset(1.0));
    }

    void presetsAreListedInOrder() {
        MilkdropPresets p;
        p.load(builtIn.path(), user.path());
        QCOMPARE(p.size(), 4);
        QCOMPARE(p.at(0).name, QStringLiteral("A-first"));
        QCOMPARE(p.at(2).name, QStringLiteral("c-third"));
        QCOMPARE(p.at(3).name, QStringLiteral("mine"));  // the user's after the built-in ones
        QVERIFY(!p.at(3).builtIn);
        QCOMPARE(p.indexOf("c-third"), 2);
        QVERIFY(p.data(1).startsWith("[preset00]"));
        QCOMPARE(p.next(3), 0);
        QCOMPARE(p.previous(0), 3);
        for (int i = 0; i < 20; ++i) QVERIFY(p.random(1) != 1);
        p.load(builtIn.path(), user.filePath("missing"));
        QCOMPARE(p.size(), 3);
    }

    void builtInPresetsAreBundled() {
        MilkdropPresets p;
        p.load(QStringLiteral(":/milkdrop"), {});
        QVERIFY2(p.size() >= 50, qPrintable(QString::number(p.size())));
        for (int i = 0; i < p.size(); ++i) QVERIFY2(p.data(i).contains("[preset"), qPrintable(p.at(i).name));
    }

    void windowSwitchesPresets() {
        MilkdropWindow w(&engine, builtIn.path(), user.path(), &skin);
        QVERIFY(!w.view());  // nothing OpenGL before it's shown
        QSignalSpy changed(&w, &MilkdropWindow::presetChanged);
        w.setShuffle(false);
        w.selectPreset(0);
        QCOMPARE(w.currentPreset(), QStringLiteral("A-first"));
        w.nextPreset();
        QCOMPARE(w.currentPreset(), QStringLiteral("b-second"));
        w.previousPreset();
        QCOMPARE(w.currentPreset(), QStringLiteral("A-first"));
        // projectM asking for the next one (time is up)...
        w.onSwitchRequested(false);
        QCOMPARE(w.currentPreset(), QStringLiteral("b-second"));
        // ...is ignored while locked.
        w.setLocked(true);
        w.onSwitchRequested(false);
        QCOMPARE(w.currentPreset(), QStringLiteral("b-second"));
        w.setLocked(false);
        // Shuffle: "previous" walks back through what was shown.
        w.setShuffle(true);
        const QString before = w.currentPreset();
        w.nextPreset();
        QVERIFY(w.currentPreset() != before);
        w.previousPreset();
        QCOMPARE(w.currentPreset(), before);
        // A preset projectM can't load is skipped.
        const QString broken = w.currentPreset();
        w.onPresetFailed(QStringLiteral("syntax error"));
        QVERIFY(w.currentPreset() != broken);
        QVERIFY(changed.count() >= 6);
    }

    void blackPresetsAreSkippedAndRemembered() {
        MilkdropWindow w(&engine, builtIn.path(), user.path(), &skin);
        w.setShuffle(false);
        w.selectPreset(0);
        w.onStaysBlack();  // A-first shows nothing on this "GPU"
        QCOMPARE(w.blackPresets(), QStringList{"A-first"});
        QCOMPARE(w.currentPreset(), QStringLiteral("b-second"));
        // Never chosen automatically again, in order...
        for (int i = 0; i < 8; ++i) {
            w.onSwitchRequested(false);
            QVERIFY(w.currentPreset() != QStringLiteral("A-first"));
        }
        w.selectPreset(1);
        w.previousPreset();  // wraps past A-first
        QCOMPARE(w.currentPreset(), QStringLiteral("mine"));
        // ...or at random.
        w.setShuffle(true);
        for (int i = 0; i < 30; ++i) {
            w.nextPreset();
            QVERIFY(w.currentPreset() != QStringLiteral("A-first"));
        }
        // By hand it can still be picked.
        w.selectPreset(0);
        QCOMPARE(w.currentPreset(), QStringLiteral("A-first"));

        // Restored from the settings.
        MilkdropWindow w2(&engine, builtIn.path(), user.path(), &skin);
        w2.setBlackPresets({"c-third"});
        QVERIFY(w2.isBlack(2));
        QVERIFY(!w2.isBlack(0));
    }

    void manyBlackInARowStopsBlamingPresets() {
        QTemporaryDir many;
        for (int i = 0; i < 12; ++i) writeFile(many.filePath(QStringLiteral("p%1.milk").arg(i, 2, 10, QChar('0'))), simplePreset(1.0));
        MilkdropWindow w(&engine, many.path(), {}, &skin);
        w.setShuffle(false);
        w.selectPreset(0);
        for (int i = 0; i < 12; ++i) w.onStaysBlack();
        // One black preset after another means the problem isn't the presets
        // (no sound reaching it, a driver issue): don't hide them all.
        QCOMPARE(w.blackPresets().size(), 5);
    }

    void rendersWithProjectM() {
        // The whole path on real OpenGL: Qt window + projectM + a real preset
        // with warp and composite shaders (bright even without sound).
        MilkdropWindow w(&engine, QStringLiteral(":/milkdrop"), user.path(), &skin);
        w.setSizeSteps({2, 4});
        w.setLocked(true);
        w.selectPreset(QStringLiteral("Geometric - RetroTrilogy(Final)"));
        QCOMPARE(w.currentPreset(), QStringLiteral("Geometric - RetroTrilogy(Final)"));
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        MilkdropView* v = w.view();
        if (!v && qEnvironmentVariableIsSet("QIYAA_EXPECT_GL")) QFAIL(qPrintable("no OpenGL: " + w.failure()));
        if (!v) QSKIP(qPrintable("no OpenGL 3.3 here: " + w.failure()));
        QVERIFY(QTest::qWaitFor([&] { return v->isReady() || !v->failure().isEmpty(); }, 5000));
        if (!v->isReady() && qEnvironmentVariableIsSet("QIYAA_EXPECT_GL")) QFAIL(qPrintable(v->failure()));
        if (!v->isReady()) QSKIP(qPrintable("no OpenGL 3.3 here: " + v->failure()));
        qInfo("OpenGL: %s", qPrintable(v->glInfo()));
        QVERIFY(v->isRendering());
        QVERIFY(QTest::qWaitFor([&] { return v->framesRendered() > 10; }, 15000));

        // A preset loaded onto a black canvas fills it in over a few seconds
        // (feedback), slower on a software renderer: keep looking for up to 20 s.
        QImage img;
        int colourCount = 0, lit = 0, samples = 0;
        QElapsedTimer t;
        t.start();
        do {
            QSignalSpy captured(v, &MilkdropView::frameCaptured);
            v->captureNextFrame();
            QVERIFY(captured.wait(5000));
            img = captured.first().first().value<QImage>();
            QSet<QRgb> colours;
            lit = samples = 0;
            for (int y = 0; y < img.height(); y += 4)
                for (int x = 0; x < img.width(); x += 4) {
                    const QRgb p = img.pixel(x, y);
                    colours.insert(p);
                    lit += qRed(p) + qGreen(p) + qBlue(p) > 30;
                    ++samples;
                }
            colourCount = int(colours.size());
            if (colourCount > 50 && lit > samples / 2) break;
            QTest::qWait(500);
        } while (t.elapsed() < 20000);
        qInfo("after %lld ms: %d colours, %d of %d lit", t.elapsed(), colourCount, lit, samples);
        QCOMPARE(img.size(), v->size() * v->devicePixelRatio());
        if (!qEnvironmentVariableIsEmpty("QIYAA_TEST_SHOTS"))
            img.save(qEnvironmentVariable("QIYAA_TEST_SHOTS") + "/milkdrop.png");
        // Something was drawn: lit and colourful, not a flat colour.
        QVERIFY2(colourCount > 50, qPrintable(QString::number(colourCount)));
        QVERIFY2(lit > samples / 2, qPrintable(QStringLiteral("%1 of %2 lit").arg(lit).arg(samples)));
        QVERIFY(qAlpha(img.pixel(img.width() / 2, img.height() / 2)) == 255);  // opaque

        // Hidden: no more frames.
        w.hide();
        QVERIFY(!v->isRendering());
    }

    void blackPictureIsNoticed() {
        // The detector on real OpenGL: a preset that draws nothing is reported,
        // one that draws a big white border isn't.
        QTemporaryDir dir;
        writeFile(dir.filePath("black.milk"),
                  "[preset00]\nfDecay=0\nfWaveAlpha=0\nnWaveMode=0\nfVideoEchoAlpha=0\nob_size=0\nob_a=0\nib_size=0\nib_a=0\nmv_a=0\nzoom=1\n");
        writeFile(dir.filePath("white.milk"), "[preset00]\nfDecay=0.9\nob_size=0.5\nob_r=1\nob_g=1\nob_b=1\nob_a=1\n");
        MilkdropWindow w(&engine, dir.path(), {}, &skin);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        MilkdropView* v = w.view();
        if (!v && qEnvironmentVariableIsSet("QIYAA_EXPECT_GL")) QFAIL(qPrintable("no OpenGL: " + w.failure()));
        if (!v) QSKIP("no OpenGL 3.3 here");
        QVERIFY(QTest::qWaitFor([&] { return v->isReady() || !v->failure().isEmpty(); }, 5000));
        if (!v->isReady()) QSKIP("no OpenGL 3.3 here");
        v->setBlackWatchTiming(300, 150, 3);
        QSignalSpy black(v, &MilkdropView::staysBlack);
        QSignalSpy picture(v, &MilkdropView::drawsPicture);
        w.setLocked(true);
        w.selectPreset(w.presets().indexOf("white"), false);
        v->setBlackWatch(true);
        QVERIFY(picture.wait(5000));
        QCOMPARE(black.count(), 0);
        w.selectPreset(w.presets().indexOf("black"), false);
        QVERIFY(black.wait(5000));
    }

    void fullScreenAndBack() {
        MilkdropWindow w(&engine, builtIn.path(), user.path(), &skin);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        if (!w.view()) QSKIP("no OpenGL 3.3 here");
        QVERIFY(QTest::qWaitFor([&] { return w.view()->isReady() || !w.view()->failure().isEmpty(); }, 5000));
        if (!w.view()->isReady()) QSKIP("no OpenGL 3.3 here");
        w.setFullScreenMode(true);
        QVERIFY(w.isFullScreenMode());
        QVERIFY(!w.view()->isRendering());  // the fullscreen view renders instead
        QTest::qWait(300);
        w.setFullScreenMode(false);
        QVERIFY(!w.isFullScreenMode());
        QVERIFY(w.view()->isRendering());
        QTest::qWait(100);  // the old view is deleted later
    }
};

QTEST_MAIN(TestMilkdrop)
#include "test_milkdrop.moc"
