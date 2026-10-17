#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>

class QNetworkAccessManager;

namespace Audio {
class AudioEngine;
}  // namespace Audio

namespace Core {
class Player;
}  // namespace Core

namespace Jam {
class HostSession;
}  // namespace Jam

namespace Telemetry {
class Reporter;
}  // namespace Telemetry

namespace Ui {
class EqualizerWindow;
class MainWindow;
class MilkdropWindow;
}  // namespace Ui

namespace Yandex {
class ApiClient;
}  // namespace Yandex

namespace App {

class Account;
class Application;
class Settings;
class WindowSet;

class TelemetryBridge : public QObject {
    Q_OBJECT
public:
    struct Sources {
        Application* application = nullptr;
        Settings* settings = nullptr;
        Account* account = nullptr;
        WindowSet* windows = nullptr;
        Core::Player* player = nullptr;
        Audio::AudioEngine* engine = nullptr;
        Yandex::ApiClient* api = nullptr;
        Ui::MainWindow* mainWindow = nullptr;
        Ui::EqualizerWindow* equalizer = nullptr;
        Ui::MilkdropWindow* milkdrop = nullptr;
        Jam::HostSession* jamHost = nullptr;
    };

    struct Config {
        QUrl url;
        QNetworkAccessManager* network = nullptr;
        QString skinOverride;
        bool audio = true;
        bool audioFailed = false;
    };

    TelemetryBridge(const Sources& sources, Config config, QObject* parent = nullptr);
    ~TelemetryBridge() override;

    bool available() const;
    bool enabled() const;
    void setEnabled(bool on);
    void start();
    void finish();
    bool isSending() const;
    Telemetry::Reporter* reporter() const;

Q_SIGNALS:
    void sent();

private:
    struct State;
    std::unique_ptr<State> state;
};

}  // namespace App
