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
class LyricsFinder;
class Player;
}  // namespace Core

namespace Jam {
class HostSession;
}  // namespace Jam

namespace SpotifyClient {
class Session;
class Web;
}  // namespace SpotifyClient

namespace Telemetry {
class Reporter;
}  // namespace Telemetry

namespace Ui {
class EqualizerWindow;
class LyricsWindow;
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
class SpotifyAccount;
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
        Core::LyricsFinder* lyrics = nullptr;
        Ui::LyricsWindow* lyricsWindow = nullptr;
        SpotifyAccount* spotify = nullptr;
        SpotifyClient::Session* spotifySession = nullptr;
        SpotifyClient::Web* spotifyWeb = nullptr;
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
    void connectSpotify(const Sources& sources);
    std::unique_ptr<State> state;
};

}  // namespace App
