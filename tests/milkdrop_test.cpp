// Milkdrop: the preset collection (no OpenGL needed) and, where an OpenGL 3.3
// context can be made (CTest runs this under Xvfb when it can), projectM
// rendering inside the window.
#include "audio/audio_engine.h"
#include "skins/skin.h"
#include "ui/milkdrop_window.h"
#include "vis/milkdrop_presets.h"
#include "vis/milkdrop_view.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
// A tiny valid Milkdrop preset: a waveform and some zoom, no shaders.
QByteArray SimplePreset(double zoom) {
    return QByteArray("[preset00]\nfDecay=0.98\nzoom=") + QByteArray::number(zoom)
        + "\nwave_r=1\nwave_g=0.5\nwave_b=0.2\nnWaveMode=2\nfWaveScale=1.5\n"
          "per_frame_1=wave_r = 0.5 + 0.5*sin(time);\n";
}
void WriteFile(const QString& path, const QByteArray& data) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
}
}  // namespace

class TestMilkdrop : public QObject {
    Q_OBJECT
private:
    QTemporaryDir builtIn, user;
    Skins::Skin skin = Skins::Skin::BuiltinBase();
    Audio::AudioEngine engine;  // not initialised: silence

private Q_SLOTS:
    void initTestCase() {
        WriteFile(builtIn.filePath("b-second.milk"), SimplePreset(1.02));
        WriteFile(builtIn.filePath("A-first.milk"), SimplePreset(0.98));
        WriteFile(builtIn.filePath("c-third.milk"), SimplePreset(1.05));
        WriteFile(builtIn.filePath("notes.txt"), "not a preset");
        WriteFile(user.filePath("mine.milk"), SimplePreset(1.0));
    }

    void presetsAreListedInOrder() {
        Vis::MilkdropPresets presets;
        presets.load(builtIn.path(), user.path());
        QCOMPARE(presets.size(), 4);
        QCOMPARE(presets.at(0).name, QStringLiteral("A-first"));
        QCOMPARE(presets.at(2).name, QStringLiteral("c-third"));
        QCOMPARE(presets.at(3).name, QStringLiteral("mine"));  // the user's after the built-in ones
        QVERIFY(!presets.at(3).builtIn);
        QCOMPARE(presets.indexOf("c-third"), 2);
        QVERIFY(presets.data(1).startsWith("[preset00]"));
        QCOMPARE(presets.next(3), 0);
        QCOMPARE(presets.previous(0), 3);
        for (int i = 0; i < 20; ++i) {
            QVERIFY(presets.random(1) != 1);
        }
        presets.load(builtIn.path(), user.filePath("missing"));
        QCOMPARE(presets.size(), 3);
    }

    void builtInPresetsAreBundled() {
        Vis::MilkdropPresets presets;
        presets.load(QStringLiteral(":/milkdrop"), {});
        QVERIFY2(presets.size() >= 50, qPrintable(QString::number(presets.size())));
        for (int i = 0; i < presets.size(); ++i) {
            QVERIFY2(presets.data(i).contains("[preset"), qPrintable(presets.at(i).name));
        }
    }

    void windowSwitchesPresets() {
        Ui::MilkdropWindow window(&engine, builtIn.path(), user.path(), &skin);
        QVERIFY(!window.view());  // nothing OpenGL before it's shown
        QSignalSpy changed(&window, &Ui::MilkdropWindow::presetChanged);
        window.setShuffle(false);
        window.selectPreset(0);
        QCOMPARE(window.currentPreset(), QStringLiteral("A-first"));
        window.nextPreset();
        QCOMPARE(window.currentPreset(), QStringLiteral("b-second"));
        window.previousPreset();
        QCOMPARE(window.currentPreset(), QStringLiteral("A-first"));
        // projectM asking for the next one (time is up)...
        window.onSwitchRequested(false);
        QCOMPARE(window.currentPreset(), QStringLiteral("b-second"));
        // ...is ignored while locked.
        window.setLocked(true);
        window.onSwitchRequested(false);
        QCOMPARE(window.currentPreset(), QStringLiteral("b-second"));
        window.setLocked(false);
        // Shuffle: "previous" walks back through what was shown.
        window.setShuffle(true);
        const QString before = window.currentPreset();
        window.nextPreset();
        QVERIFY(window.currentPreset() != before);
        window.previousPreset();
        QCOMPARE(window.currentPreset(), before);
        // A preset projectM can't load is skipped.
        const QString broken = window.currentPreset();
        window.onPresetFailed(QStringLiteral("syntax error"));
        QVERIFY(window.currentPreset() != broken);
        QVERIFY(changed.count() >= 6);
    }

    void blackPresetsAreSkippedAndRemembered() {
        Ui::MilkdropWindow window(&engine, builtIn.path(), user.path(), &skin);
        window.setShuffle(false);
        window.selectPreset(0);
        window.onStaysBlack();  // A-first shows nothing on this "GPU"
        QCOMPARE(window.blackPresets(), QStringList{"A-first"});
        QCOMPARE(window.currentPreset(), QStringLiteral("b-second"));
        // Never chosen automatically again, in order...
        for (int i = 0; i < 8; ++i) {
            window.onSwitchRequested(false);
            QVERIFY(window.currentPreset() != QStringLiteral("A-first"));
        }
        window.selectPreset(1);
        window.previousPreset();  // wraps past A-first
        QCOMPARE(window.currentPreset(), QStringLiteral("mine"));
        // ...or at random (many times: a black one must never come up, not just rarely).
        window.setShuffle(true);
        for (int i = 0; i < 1000; ++i) {
            window.nextPreset();
            QVERIFY(window.currentPreset() != QStringLiteral("A-first"));
        }
        // By hand it can still be picked.
        window.selectPreset(0);
        QCOMPARE(window.currentPreset(), QStringLiteral("A-first"));

        // All the others black: stay on the one that shows something, in both modes.
        window.setBlackPresets({"A-first", "b-second", "mine"});
        window.selectPreset(2);
        for (bool shuffle : {true, false}) {
            window.setShuffle(shuffle);
            for (int i = 0; i < 20; ++i) {
                window.nextPreset();
                QCOMPARE(window.currentPreset(), QStringLiteral("c-third"));
            }
        }
        // All black: still moves on rather than getting stuck.
        window.setBlackPresets({"A-first", "b-second", "c-third", "mine"});
        window.setShuffle(false);
        window.nextPreset();
        QCOMPARE(window.currentPreset(), QStringLiteral("mine"));

        // Restored from the settings.
        Ui::MilkdropWindow w2(&engine, builtIn.path(), user.path(), &skin);
        w2.setBlackPresets({"c-third"});
        QVERIFY(w2.isBlack(2));
        QVERIFY(!w2.isBlack(0));
    }

    void manyBlackInARowStopsBlamingPresets() {
        QTemporaryDir many;
        for (int i = 0; i < 12; ++i) {
            WriteFile(
                many.filePath(QStringLiteral("p%1.milk").arg(i, 2, 10, QChar('0'))),
                SimplePreset(1.0)
            );
        }
        Ui::MilkdropWindow window(&engine, many.path(), {}, &skin);
        window.setShuffle(false);
        window.selectPreset(0);
        for (int i = 0; i < 12; ++i) {
            window.onStaysBlack();
        }
        // One black preset after another means the problem isn't the presets
        // (no sound reaching it, a driver issue): don't hide them all.
        QCOMPARE(window.blackPresets().size(), 5);
    }

    void rendersWithProjectM() {
        // The whole path on real OpenGL: Qt window + projectM + a real preset
        // with warp and composite shaders (bright even without sound).
        Ui::MilkdropWindow window(&engine, QStringLiteral(":/milkdrop"), user.path(), &skin);
        window.setSizeSteps({2, 4});
        window.setLocked(true);
        window.selectPreset(QStringLiteral("Geometric - RetroTrilogy(Final)"));
        QCOMPARE(window.currentPreset(), QStringLiteral("Geometric - RetroTrilogy(Final)"));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        Vis::MilkdropView* view = window.view();
        if (!view && qEnvironmentVariableIsSet("QIYAA_EXPECT_GL")) {
            QFAIL(qPrintable("no OpenGL: " + window.failure()));
        }
        if (!view) {
            QSKIP(qPrintable("no OpenGL 3.3 here: " + window.failure()));
        }
        QVERIFY(QTest::qWaitFor([&] { return view->isReady() || !view->failure().isEmpty(); }, 5000)
        );
        if (!view->isReady() && qEnvironmentVariableIsSet("QIYAA_EXPECT_GL")) {
            QFAIL(qPrintable(view->failure()));
        }
        if (!view->isReady()) {
            QSKIP(qPrintable("no OpenGL 3.3 here: " + view->failure()));
        }
        qInfo("OpenGL: %s", qPrintable(view->glInfo()));
        QVERIFY(view->isRendering());
        QVERIFY(QTest::qWaitFor([&] { return view->framesRendered() > 10; }, 15000));

        // A preset loaded onto a black canvas fills it in over a few seconds
        // (feedback), slower on a software renderer: keep looking for up to 20 s.
        QImage image;
        int colourCount = 0, lit = 0, samples = 0;
        QElapsedTimer timer;
        timer.start();
        do {
            QSignalSpy captured(view, &Vis::MilkdropView::frameCaptured);
            view->captureNextFrame();
            QVERIFY(captured.wait(5000));
            image = captured.first().first().value<QImage>();
            QSet<QRgb> colours;
            lit = samples = 0;
            for (int y = 0; y < image.height(); y += 4) {
                for (int x = 0; x < image.width(); x += 4) {
                    const QRgb p = image.pixel(x, y);
                    colours.insert(p);
                    lit += qRed(p) + qGreen(p) + qBlue(p) > 30;
                    ++samples;
                }
            }
            colourCount = int(colours.size());
            if (colourCount > 50 && lit > samples / 2) {
                break;
            }
            QTest::qWait(500);
        } while (timer.elapsed() < 20000);
        qInfo(
            "after %lld ms: %d colours, %d of %d lit", timer.elapsed(), colourCount, lit, samples
        );
        QCOMPARE(image.size(), view->size() * view->devicePixelRatio());
        if (!qEnvironmentVariableIsEmpty("QIYAA_TEST_SHOTS")) {
            image.save(qEnvironmentVariable("QIYAA_TEST_SHOTS") + "/milkdrop.png");
        }
        // Something was drawn: lit and colourful, not a flat colour.
        QVERIFY2(colourCount > 50, qPrintable(QString::number(colourCount)));
        QVERIFY2(
            lit > samples / 2, qPrintable(QStringLiteral("%1 of %2 lit").arg(lit).arg(samples))
        );
        QVERIFY(qAlpha(image.pixel(image.width() / 2, image.height() / 2)) == 255);  // opaque

        // Hidden: no more frames.
        window.hide();
        QVERIFY(!view->isRendering());
    }

    void blackPictureIsNoticed() {
        // The detector on real OpenGL: a preset that draws nothing is reported,
        // one that draws a big white border isn't.
        QTemporaryDir dir;
        WriteFile(
            dir.filePath("black.milk"),
            "[preset00]\nfDecay=0\nfWaveAlpha=0\nnWaveMode=0\nfVideoEchoAlpha=0\nob_size=0\nob_a="
            "0\nib_size=0\nib_a=0\nmv_a=0\nzoom=1\n"
        );
        WriteFile(
            dir.filePath("white.milk"),
            "[preset00]\nfDecay=0.9\nob_size=0.5\nob_r=1\nob_g=1\nob_b=1\nob_a=1\n"
        );
        Ui::MilkdropWindow window(&engine, dir.path(), {}, &skin);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        Vis::MilkdropView* view = window.view();
        if (!view && qEnvironmentVariableIsSet("QIYAA_EXPECT_GL")) {
            QFAIL(qPrintable("no OpenGL: " + window.failure()));
        }
        if (!view) {
            QSKIP("no OpenGL 3.3 here");
        }
        QVERIFY(QTest::qWaitFor([&] { return view->isReady() || !view->failure().isEmpty(); }, 5000)
        );
        if (!view->isReady()) {
            QSKIP("no OpenGL 3.3 here");
        }
        view->setBlackWatchTiming(300, 150, 3);
        QSignalSpy black(view, &Vis::MilkdropView::staysBlack);
        QSignalSpy picture(view, &Vis::MilkdropView::drawsPicture);
        window.setLocked(true);
        window.selectPreset(window.presets().indexOf("white"), false);
        view->setBlackWatch(true);
        QVERIFY(picture.wait(5000));
        QCOMPARE(black.count(), 0);
        window.selectPreset(window.presets().indexOf("black"), false);
        QVERIFY(black.wait(5000));
    }

    void fullScreenAndBack() {
        Ui::MilkdropWindow window(&engine, builtIn.path(), user.path(), &skin);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        if (!window.view()) {
            QSKIP("no OpenGL 3.3 here");
        }
        QVERIFY(QTest::qWaitFor(
            [&] { return window.view()->isReady() || !window.view()->failure().isEmpty(); }, 5000
        ));
        if (!window.view()->isReady()) {
            QSKIP("no OpenGL 3.3 here");
        }
        window.setFullScreenMode(true);
        QVERIFY(window.isFullScreenMode());
        QVERIFY(!window.view()->isRendering());  // the fullscreen view renders instead
        QTest::qWait(300);
        window.setFullScreenMode(false);
        QVERIFY(!window.isFullScreenMode());
        QVERIFY(window.view()->isRendering());
        QTest::qWait(100);  // the old view is deleted later
    }
};

QTEST_MAIN(TestMilkdrop)
#include "milkdrop_test.moc"
