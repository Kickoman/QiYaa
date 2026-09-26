#include "app/application.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QIcon>
#include <QTimer>

#include <cstdio>
#include <exception>
#include <memory>

namespace {

constexpr int kSuccess = 0;
constexpr int kFailure = 1;
constexpr int kInternalError = 2;

void ChoosePlatform() {
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    if (qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        return;
    }
    if (qEnvironmentVariable("QIYAA_NATIVE_WAYLAND") == QLatin1String("1")) {
        return;
    }
    if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY")
        || qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland")) {
        qputenv("QT_QPA_PLATFORM", "xcb;wayland");
    }
#endif
}

void StreamLocalFile(Audio::AudioEngine* engine, const QString& path) {
    auto file = std::make_shared<QFile>(path);
    if (!file->open(QIODevice::ReadOnly)) {
        qWarning("Cannot open %s", qPrintable(path));
        return;
    }
    engine->beginStream();
    auto* timer = new QTimer(engine);
    QObject::connect(timer, &QTimer::timeout, engine, [engine, file, timer] {
        const QByteArray chunk = file->read(64 * 1024);
        if (!chunk.isEmpty()) {
            engine->appendData(chunk);
        }
        if (file->atEnd()) {
            engine->finishData();
            timer->deleteLater();
        }
    });
    timer->start(20);
}

QList<Yandex::Track> DemoTracks() {
    const std::pair<const char*, int> samples[] = {
        {"Кино - Группа крови", 285},       {"Земфира - Искала", 237},
        {"Сплин - Выхода нет", 227},        {"Björk - Jóga", 305},
        {"Daft Punk - Digital Love", 301},  {"Мумий Тролль - Владивосток 2000", 164},
        {"Radiohead - Karma Police", 264},  {"Кино - Кукушка", 395},
        {"Nirvana - Come As You Are", 219},
    };
    QList<Yandex::Track> out;
    int id = 1;
    for (const auto& [name, seconds] : samples) {
        Yandex::Track track;
        const QString text = QString::fromUtf8(name);
        track.id = QString::number(id++);
        track.artists << text.section(QStringLiteral(" - "), 0, 0);
        track.title = text.section(QStringLiteral(" - "), 1);
        track.durationMs = seconds * 1000;
        out << track;
    }
    return out;
}

int Run(int& argc, char* argv[]) {
    ChoosePlatform();
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("QiYaa"));
    QGuiApplication::setDesktopFileName(QStringLiteral("qiyaa")
    );  // matches qiyaa.desktop (taskbar icon, MPRIS)
    {
        QIcon icon;
        for (int size : {16, 24, 32, 48, 64, 128, 256}) {
            icon.addFile(QStringLiteral(":/icons/qiyaa-%1.png").arg(size));
        }
        QApplication::setWindowIcon(icon);
    }
    QApplication::setApplicationVersion(QStringLiteral(QIYAA_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser commandLine;
    commandLine.setApplicationDescription(QStringLiteral("Winamp-style Yandex Music player"));
    commandLine.addHelpOption();
    commandLine.addVersionOption();
    QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"), QStringLiteral("Render the windows to <png> and exit."),
        QStringLiteral("png")
    );
    QCommandLineOption skinOption(
        QStringLiteral("skin"), QStringLiteral("Use skin <wsz> for this run."),
        QStringLiteral("wsz")
    );
    QCommandLineOption fileOption(
        QStringLiteral("play-file"),
        QStringLiteral("Play a local audio file instead of Yandex Music."), QStringLiteral("path")
    );
    QCommandLineOption offlineOption(
        QStringLiteral("offline"), QStringLiteral("Don't connect to Yandex Music.")
    );
    QCommandLineOption textOption(
        QStringLiteral("text"), QStringLiteral("Show <text> in the marquee."),
        QStringLiteral("text")
    );
    QCommandLineOption demoOption(
        QStringLiteral("demo"),
        QStringLiteral("Fill the playlist with sample entries (UI testing).")
    );
    QCommandLineOption scaleOption(
        QStringLiteral("scale"), QStringLiteral("Window size for this run, e.g. 1.5."),
        QStringLiteral("factor")
    );
    commandLine.addOptions(
        {screenshotOption, skinOption, fileOption, offlineOption, textOption, demoOption,
         scaleOption}
    );
    commandLine.process(app);

    const bool screenshot = commandLine.isSet(screenshotOption);
    App::Application::Options options;
    options.skinOverride = commandLine.value(skinOption);
    options.offline =
        screenshot || commandLine.isSet(offlineOption) || commandLine.isSet(fileOption);
    options.audio = !screenshot || commandLine.isSet(fileOption);
    options.readOnlySettings = screenshot;
    options.mediaIntegration = !screenshot;
    App::Application application(options);

    application.start();
    if (commandLine.isSet(scaleOption)) {
        application.setScale(commandLine.value(scaleOption).toDouble(), /*persist=*/false);
    }
    if (commandLine.isSet(demoOption)) {
        application.player()->setQueue(DemoTracks(), QStringLiteral("Demo"), false);
    }
    if (commandLine.isSet(textOption)) {
        application.mainWindow()->setStatusText(commandLine.value(textOption));
    }

    if (screenshot && !commandLine.isSet(fileOption)) {
        QApplication::processEvents();
        return application.snapshot().save(commandLine.value(screenshotOption)) ? kSuccess
                                                                                : kFailure;
    }

    if (commandLine.isSet(fileOption)) {
        StreamLocalFile(application.engine(), commandLine.value(fileOption));
    }
    if (screenshot) {
        QTimer::singleShot(1500, &app, [&] {
            const bool ok = application.snapshot().save(commandLine.value(screenshotOption));
            QApplication::exit(ok ? kSuccess : kFailure);
        });
    }
    return app.exec();
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        return Run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "QiYaa: internal error: %s\n", error.what());
        return kInternalError;
    }
}
