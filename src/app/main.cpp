#include "app/application.h"
#include "app/offline_sources.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QLatin1String>
#include <QString>
#include <QTimer>

#include <cstdio>
#include <exception>

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

int Run(int& argc, char* argv[]) {
    ChoosePlatform();
    QApplication qtApplication(argc, argv);
    QApplication::setApplicationName(QStringLiteral("QiYaa"));
    QGuiApplication::setDesktopFileName(QStringLiteral("qiyaa"));
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
    commandLine.process(qtApplication);

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
        application.setScale(
            commandLine.value(scaleOption).toDouble(), App::Application::ScaleScope::ThisRun
        );
    }
    if (commandLine.isSet(demoOption)) {
        application.player()->setQueue(App::DemoTracks(), QStringLiteral("Demo"), false);
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
        App::StreamLocalFile(application.engine(), commandLine.value(fileOption));
    }
    if (screenshot) {
        QTimer::singleShot(1500, &qtApplication, [&] {
            const bool ok = application.snapshot().save(commandLine.value(screenshotOption));
            QApplication::exit(ok ? kSuccess : kFailure);
        });
    }
    return qtApplication.exec();
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
