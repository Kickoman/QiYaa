#include "app/settings.h"

#include <QFile>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QVariant>

// App::Settings: every key of settings.ini, its default and the value it writes, as the table in
// src/app/README.md lists them.

class TestSettings : public QObject {
    Q_OBJECT
private:
    QTemporaryDir directory;
    QString file;

    QVariant stored(const QString& key) const { return QSettings(file, QSettings::IniFormat).value(key); }

    void store(const QString& key, const QVariant& value) const {
        QSettings settings(file, QSettings::IniFormat);
        settings.setValue(key, value);
    }

private Q_SLOTS:
    void init() {
        QVERIFY(directory.isValid());
        file = directory.filePath(QStringLiteral("settings-%1.ini").arg(QTest::currentTestFunction()));
    }

    void anEmptyFileGivesTheDefaults() {
        const App::Settings settings(file);
        QVERIFY(settings.wasEmpty());
        QCOMPARE(settings.directory(), directory.path());
        QVERIFY(!settings.language());
        QCOMPARE(settings.skin(), QString());
        QCOMPARE(settings.scale(), 1.0);
        QVERIFY(!settings.alwaysOnTop());
        QCOMPARE(settings.shuffleAlgorithm(), Core::ShuffleAlgorithm::WithoutRepeats);
        QCOMPARE(settings.quality(), Yandex::Quality::Standard);
        QCOMPARE(settings.lastLocalFolder(), QString());
        QVERIFY(settings.continueWithVibe());
        const App::MainWindowState mainWindow = settings.mainWindow();
        QCOMPARE(mainWindow.volume, 75);
        QCOMPARE(mainWindow.balance, 0);
        QCOMPARE(mainWindow.visMode, 0);
        QVERIFY(!mainWindow.remainingTime);
        QCOMPARE(settings.equalizer(), Audio::EqSettings{});
        QVERIFY(!settings.equalizerAuto());
        const App::MilkdropSettings milkdrop = settings.milkdrop();
        QVERIFY(milkdrop.shuffle);
        QVERIFY(!milkdrop.locked);
        QCOMPARE(milkdrop.seconds, 30);
        QVERIFY(milkdrop.preset.isEmpty() && milkdrop.black.isEmpty());
        const App::JamSettings jam = settings.jam();
        QCOMPARE(jam.server, App::Settings::DefaultJamServer());
        QVERIFY(jam.waveFeedback);
        QVERIFY(!jam.shareAudio);
        QVERIFY(settings.telemetryEnabled());
        QVERIFY(!settings.hasFirstStart());
        QVERIFY(settings.machine().isEmpty());
        QVERIFY(!QFile::exists(file));
    }

    void aWindowTakesItsDefaultsUntilSaved() {
        App::Settings settings(file);
        App::WindowState playlist = settings.window(QStringLiteral("playlist"), true, QSize(0, 4));
        QVERIFY(!playlist.pos);
        QVERIFY(playlist.visible);
        QVERIFY(!playlist.shaded);
        QCOMPARE(playlist.steps, QSize(0, 4));
        settings.setWindowPos(QStringLiteral("playlist"), QPoint(10, 20));
        settings.setWindowVisible(QStringLiteral("playlist"), false);
        settings.setWindowShaded(QStringLiteral("playlist"), true);
        settings.setWindowSteps(QStringLiteral("playlist"), QSize(2, 3));
        QCOMPARE(stored(QStringLiteral("playlist/pos")), QVariant(QPoint(10, 20)));
        QCOMPARE(stored(QStringLiteral("playlist/visible")), QVariant(false));
        QCOMPARE(stored(QStringLiteral("playlist/shaded")), QVariant(true));
        QCOMPARE(stored(QStringLiteral("playlist/steps")), QVariant(QSize(2, 3)));
        playlist = settings.window(QStringLiteral("playlist"), true, QSize(0, 4));
        QCOMPARE(playlist.pos, std::optional(QPoint(10, 20)));
        QVERIFY(!playlist.visible);
        QVERIFY(playlist.shaded);
        QCOMPARE(playlist.steps, QSize(2, 3));
        QCOMPARE(settings.window(QStringLiteral("jamWindow"), false, QSize(1, 8)).steps, QSize(1, 8));
    }

    void eachValueGoesUnderItsKey() {
        App::Settings settings(file);
        settings.setLanguage(App::Language::Russian);
        settings.setSkin(QStringLiteral(":/skins/Vizor1-01.wsz"));
        settings.setScale(1.5);
        settings.setAlwaysOnTop(true);
        settings.setShuffleAlgorithm(Core::ShuffleAlgorithm::Random);
        settings.setQuality(Yandex::Quality::Economy);
        settings.setLastLocalFolder(QStringLiteral("/home/маша/Music"));
        settings.setMainWindow(App::MainWindowState{40, -20, 1, true});
        Audio::EqSettings eq;
        eq.enabled = false;
        eq.preampDb = -3.0;
        eq.bandsDb[0] = 3.0;
        eq.bandsDb[9] = 1.5;
        settings.setEqualizer(eq);
        settings.setEqualizerAuto(true);
        settings.setMilkdrop(App::MilkdropSettings{false, true, 90, QStringLiteral("Flexi"), {QStringLiteral("Dark")}});
        settings.setJam(App::JamSettings{QStringLiteral("https://jam.example.org"), false, true});
        settings.setTelemetryEnabled(false);
        settings.setMachine(QStringLiteral("machine-1"));

        QCOMPARE(stored(QStringLiteral("language")), QVariant(QStringLiteral("ru")));
        QCOMPARE(stored(QStringLiteral("skin")), QVariant(QStringLiteral(":/skins/Vizor1-01.wsz")));
        QCOMPARE(stored(QStringLiteral("scale")).toDouble(), 1.5);
        QCOMPARE(stored(QStringLiteral("alwaysOnTop")), QVariant(true));
        QCOMPARE(stored(QStringLiteral("shuffle/algorithm")), QVariant(QStringLiteral("random")));
        QCOMPARE(stored(QStringLiteral("playback/quality")), QVariant(QStringLiteral("economy")));
        QCOMPARE(stored(QStringLiteral("local/lastFolder")), QVariant(QStringLiteral("/home/маша/Music")));
        QCOMPARE(stored(QStringLiteral("volume")), QVariant(40));
        QCOMPARE(stored(QStringLiteral("balance")), QVariant(-20));
        QCOMPARE(stored(QStringLiteral("vis/mode")), QVariant(1));
        QCOMPARE(stored(QStringLiteral("time/remaining")), QVariant(true));
        QCOMPARE(stored(QStringLiteral("equalizer/enabled")), QVariant(false));
        QCOMPARE(stored(QStringLiteral("equalizer/preamp")).toDouble(), -3.0);
        const QStringList bands{"3.0", "0.0", "0.0", "0.0", "0.0", "0.0", "0.0", "0.0", "0.0", "1.5"};
        QCOMPARE(stored(QStringLiteral("equalizer/bands")).toStringList(), bands);
        QCOMPARE(stored(QStringLiteral("equalizer/auto")), QVariant(true));
        QCOMPARE(stored(QStringLiteral("milkdrop/shuffle")), QVariant(false));
        QCOMPARE(stored(QStringLiteral("milkdrop/locked")), QVariant(true));
        QCOMPARE(stored(QStringLiteral("milkdrop/seconds")), QVariant(90));
        QCOMPARE(stored(QStringLiteral("milkdrop/preset")), QVariant(QStringLiteral("Flexi")));
        QCOMPARE(stored(QStringLiteral("milkdrop/black")).toStringList(), QStringList{QStringLiteral("Dark")});
        QCOMPARE(stored(QStringLiteral("jam/server")), QVariant(QStringLiteral("https://jam.example.org")));
        QCOMPARE(stored(QStringLiteral("jam/waveFeedback")), QVariant(false));
        QCOMPARE(stored(QStringLiteral("jam/shareAudio")), QVariant(true));
        QCOMPARE(stored(QStringLiteral("telemetry/enabled")), QVariant(false));
        QCOMPARE(stored(QStringLiteral("telemetry/machine")), QVariant(QStringLiteral("machine-1")));

        const App::Settings reread(file);
        QVERIFY(!reread.wasEmpty());
        QCOMPARE(reread.language(), std::optional(App::Language::Russian));
        QCOMPARE(reread.shuffleAlgorithm(), Core::ShuffleAlgorithm::Random);
        QCOMPARE(reread.quality(), Yandex::Quality::Economy);
        QCOMPARE(reread.lastLocalFolder(), QStringLiteral("/home/маша/Music"));
        QCOMPARE(reread.equalizer(), eq);
        QCOMPARE(reread.milkdrop().preset, QStringLiteral("Flexi"));
        QCOMPARE(reread.jam().server, QStringLiteral("https://jam.example.org"));
    }

    void theFirstStartIsOnDiskAtOnce() {
        App::Settings settings(file);
        settings.setFirstStart(QDate(2026, 10, 17));
        QVERIFY(settings.hasFirstStart());
        QFile written(file);
        QVERIFY(written.open(QIODevice::ReadOnly));
        QVERIFY(written.readAll().contains("firstStart=2026-10-17"));
    }

    void unknownValuesFallBack() {
        store(QStringLiteral("shuffle/algorithm"), QStringLiteral("unknown"));
        store(QStringLiteral("playback/quality"), QStringLiteral("lossless"));
        store(QStringLiteral("language"), QStringLiteral("de"));
        store(QStringLiteral("vis/mode"), 7);
        store(QStringLiteral("equalizer/bands"), QStringList{QStringLiteral("2.5")});
        store(QStringLiteral("jam/server"), QStringLiteral("  https://jam.example.org  "));
        const App::Settings settings(file);
        QCOMPARE(settings.shuffleAlgorithm(), Core::ShuffleAlgorithm::WithoutRepeats);
        QCOMPARE(settings.quality(), Yandex::Quality::Standard);
        QVERIFY(!settings.language());
        QCOMPARE(settings.mainWindow().visMode, 2);
        const Audio::EqSettings eq = settings.equalizer();
        QCOMPARE(eq.bandsDb[0], 2.5);
        QCOMPARE(eq.bandsDb[1], 0.0);
        QCOMPARE(settings.jam().server, QStringLiteral("https://jam.example.org"));
    }
};

QTEST_GUILESS_MAIN(TestSettings)
#include "settings_test.moc"
