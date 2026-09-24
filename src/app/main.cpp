#include <memory>

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QTimer>

#include "app/Paths.h"
#include "audio/AudioEngine.h"
#include "core/Player.h"
#include "skin/Skin.h"
#include "ui/MainWindow.h"
#include "yandex/ApiClient.h"

using namespace qiyaa;

namespace {

// Wayland doesn't let apps position their own windows, which the Winamp layout
// (several snapped windows) depends on. Unless the user explicitly chose a
// platform, run through XWayland; fall back to native Wayland if xcb is missing.
void choosePlatform() {
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    if (qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) return;
    if (qEnvironmentVariable("QIYAA_NATIVE_WAYLAND") == QLatin1String("1")) return;
    if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY") || qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland"))
        qputenv("QT_QPA_PLATFORM", "xcb;wayland");
#endif
}

// Feeds a local file into the engine in small pieces, like a slow download.
// Handy for testing audio without a Yandex account.
void streamLocalFile(audio::AudioEngine* engine, const QString& path) {
    auto file = std::make_shared<QFile>(path);
    if (!file->open(QIODevice::ReadOnly)) {
        qWarning("Cannot open %s", qPrintable(path));
        return;
    }
    engine->beginStream();
    auto* timer = new QTimer(engine);
    QObject::connect(timer, &QTimer::timeout, engine, [engine, file, timer] {
        const QByteArray chunk = file->read(64 * 1024);
        if (!chunk.isEmpty()) engine->appendData(chunk);
        if (file->atEnd()) {
            engine->finishData();
            timer->deleteLater();
        }
    });
    timer->start(20);
}

}  // namespace

int main(int argc, char* argv[]) {
    choosePlatform();
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("QiYaa"));
    QApplication::setApplicationVersion(QStringLiteral(QIYAA_VERSION));
    QApplication::setQuitOnLastWindowClosed(true);

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Winamp-style Yandex Music player"));
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption screenshotOpt(QStringLiteral("screenshot"), QStringLiteral("Render the main window to <png> and exit."),
                                     QStringLiteral("png"));
    QCommandLineOption skinOpt(QStringLiteral("skin"), QStringLiteral("Use skin <wsz> for this run."), QStringLiteral("wsz"));
    QCommandLineOption fileOpt(QStringLiteral("play-file"), QStringLiteral("Play a local audio file instead of Yandex Music."),
                               QStringLiteral("path"));
    QCommandLineOption offlineOpt(QStringLiteral("offline"), QStringLiteral("Don't connect to Yandex Music."));
    QCommandLineOption textOpt(QStringLiteral("text"), QStringLiteral("Show <text> in the marquee (for screenshots)."),
                               QStringLiteral("text"));
    cli.addOptions({screenshotOpt, skinOpt, fileOpt, offlineOpt, textOpt});
    cli.process(app);

    QSettings settings(paths::configDir() + QStringLiteral("/settings.ini"), QSettings::IniFormat);

    // Skins: the base skin fills in any sheets a custom skin lacks.
    const Skin baseSkin = Skin::builtinBase();
    auto currentSkin = std::make_unique<Skin>();
    auto loadSkin = [&](const QString& path) -> bool {
        if (path.isEmpty()) return false;
        auto s = std::make_unique<Skin>();
        QString err;
        if (!s->loadFromFile(path, &baseSkin, &err)) {
            qWarning("Cannot load skin %s: %s", qPrintable(path), qPrintable(err));
            return false;
        }
        currentSkin = std::move(s);
        return true;
    };
    const QString skinPath = cli.isSet(skinOpt) ? cli.value(skinOpt) : settings.value(QStringLiteral("skin")).toString();
    if (!loadSkin(skinPath)) *currentSkin = baseSkin;

    QNetworkAccessManager nam;
    yandex::ApiClient api(&nam);
    audio::AudioEngine engine;
    if (!cli.isSet(screenshotOpt)) {
        QString audioError;
        if (!engine.init(&audioError)) qWarning("Audio: %s", qPrintable(audioError));
        else qInfo("Audio backend: %s", qPrintable(engine.backendName()));
    }
    Player player(&api, &engine);

    MainWindow window(&player, currentSkin.get());
    window.setVolume(settings.value(QStringLiteral("volume"), 75).toInt());
    window.setBalance(settings.value(QStringLiteral("balance"), 0).toInt());
    window.setScale(settings.value(QStringLiteral("scale"), 1.0).toDouble());
    if (settings.value(QStringLiteral("alwaysOnTop"), false).toBool())
        window.setWindowFlag(Qt::WindowStaysOnTopHint, true);

    QObject::connect(&window, &MainWindow::skinRequested, &window, [&](const QString& path) {
        if (loadSkin(path)) {
            window.setSkin(currentSkin.get());
            if (!cli.isSet(screenshotOpt)) settings.setValue(QStringLiteral("skin"), path);
        } else {
            window.setStatusText(QStringLiteral("Cannot load skin"));
        }
    });
    QObject::connect(&window, &MainWindow::scaleRequested, &window, [&](double s) {
        window.setScale(s);
        settings.setValue(QStringLiteral("scale"), s);
    });
    QObject::connect(&window, &MainWindow::alwaysOnTopRequested, &window, [&](bool on) {
        window.setWindowFlag(Qt::WindowStaysOnTopHint, on);
        window.show();
        settings.setValue(QStringLiteral("alwaysOnTop"), on);
    });
    QObject::connect(&window, &SkinnedWindow::moveFinished, &window, [&](QPoint pos) {
        settings.setValue(QStringLiteral("mainWindow/pos"), pos);
    });
    QObject::connect(&app, &QApplication::aboutToQuit, &window, [&] {
        settings.setValue(QStringLiteral("volume"), window.volume());
        settings.setValue(QStringLiteral("balance"), window.balance());
        if (SkinnedWindow::canPositionWindows()) settings.setValue(QStringLiteral("mainWindow/pos"), window.pos());
        player.stop();
    });

    if (cli.isSet(screenshotOpt)) {
        if (cli.isSet(textOpt)) window.setStatusText(cli.value(textOpt));
        window.grab().save(cli.value(screenshotOpt));
        return 0;
    }

    window.show();
    window.placeAt(settings.value(QStringLiteral("mainWindow/pos"), QPoint(100, 100)).toPoint());

    if (cli.isSet(fileOpt)) streamLocalFile(&engine, cli.value(fileOpt));
    else if (!cli.isSet(offlineOpt)) player.start();

    return app.exec();
}
