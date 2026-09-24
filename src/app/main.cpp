#include <memory>

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QTimer>

#include "app/App.h"
#include "ui/MainWindow.h"

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

// A few fake tracks, for screenshots and UI testing without an account.
QList<yandex::Track> demoTracks() {
    const std::pair<const char*, int> raw[] = {
        {"Кино - Группа крови", 285},      {"Земфира - Искала", 237},         {"Сплин - Выхода нет", 227},
        {"Björk - Jóga", 305},             {"Daft Punk - Digital Love", 301}, {"Мумий Тролль - Владивосток 2000", 164},
        {"Radiohead - Karma Police", 264}, {"Кино - Кукушка", 395},           {"Nirvana - Come As You Are", 219},
    };
    QList<yandex::Track> out;
    int id = 1;
    for (const auto& [name, secs] : raw) {
        yandex::Track t;
        const QString s = QString::fromUtf8(name);
        t.id = QString::number(id++);
        t.artists << s.section(QStringLiteral(" - "), 0, 0);
        t.title = s.section(QStringLiteral(" - "), 1);
        t.durationMs = secs * 1000;
        out << t;
    }
    return out;
}

}  // namespace

int main(int argc, char* argv[]) {
    choosePlatform();
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("QiYaa"));
    QApplication::setApplicationVersion(QStringLiteral(QIYAA_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);  // closing the EQ/playlist must not quit

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Winamp-style Yandex Music player"));
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption screenshotOpt(QStringLiteral("screenshot"), QStringLiteral("Render the windows to <png> and exit."),
                                     QStringLiteral("png"));
    QCommandLineOption skinOpt(QStringLiteral("skin"), QStringLiteral("Use skin <wsz> for this run."), QStringLiteral("wsz"));
    QCommandLineOption fileOpt(QStringLiteral("play-file"), QStringLiteral("Play a local audio file instead of Yandex Music."),
                               QStringLiteral("path"));
    QCommandLineOption offlineOpt(QStringLiteral("offline"), QStringLiteral("Don't connect to Yandex Music."));
    QCommandLineOption textOpt(QStringLiteral("text"), QStringLiteral("Show <text> in the marquee."), QStringLiteral("text"));
    QCommandLineOption demoOpt(QStringLiteral("demo"), QStringLiteral("Fill the playlist with sample entries (UI testing)."));
    QCommandLineOption scaleOpt(QStringLiteral("scale"), QStringLiteral("Window size for this run, e.g. 1.5."), QStringLiteral("factor"));
    cli.addOptions({screenshotOpt, skinOpt, fileOpt, offlineOpt, textOpt, demoOpt, scaleOpt});
    cli.process(app);

    const bool screenshot = cli.isSet(screenshotOpt);
    App::Options opts;
    opts.skinOverride = cli.value(skinOpt);
    opts.offline = screenshot || cli.isSet(offlineOpt) || cli.isSet(fileOpt);
    // With --play-file, a screenshot is taken after a second of playback (shows the visualizer).
    opts.audio = !screenshot || cli.isSet(fileOpt);
    opts.readOnlySettings = screenshot;  // screenshots never touch the user's settings
    App qiyaa(opts);

    qiyaa.start();
    if (cli.isSet(scaleOpt)) qiyaa.setScale(cli.value(scaleOpt).toDouble(), /*persist=*/false);
    if (cli.isSet(demoOpt)) qiyaa.player()->setQueue(demoTracks(), QStringLiteral("Demo"), false);
    if (cli.isSet(textOpt)) qiyaa.mainWindow()->setStatusText(cli.value(textOpt));

    if (screenshot && !cli.isSet(fileOpt)) {
        QApplication::processEvents();
        return qiyaa.snapshot().save(cli.value(screenshotOpt)) ? 0 : 1;
    }

    if (cli.isSet(fileOpt)) streamLocalFile(qiyaa.engine(), cli.value(fileOpt));
    if (screenshot) {
        QTimer::singleShot(1500, &app, [&] {
            const bool ok = qiyaa.snapshot().save(cli.value(screenshotOpt));
            QApplication::exit(ok ? 0 : 1);
        });
    }
    return app.exec();
}
