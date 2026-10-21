#pragma once

#include "app/translations.h"

#include <QObject>
#include <QPoint>

class QMenu;
class QWidget;

namespace Audio {
class AudioEngine;
}  // namespace Audio

namespace Core {
class LyricsFinder;
class Player;
class SourceRegistry;
struct Track;
}  // namespace Core

namespace Ui {
class MainWindow;
class PlaylistWindow;
}  // namespace Ui

namespace Yandex {
class Library;
}  // namespace Yandex

namespace LocalSource {
class Catalog;
}  // namespace LocalSource

namespace YandexSource {
class Catalog;
class Source;
}  // namespace YandexSource

namespace App {

class Account;
class JamBridge;
class Settings;
class TelemetryBridge;
class WindowSet;

void TransportKey(Core::Player* player, Audio::AudioEngine* engine, int key);

class MainMenu : public QObject {
    Q_OBJECT
public:
    struct Parts {
        Core::Player* player = nullptr;
        Audio::AudioEngine* engine = nullptr;
        Yandex::Library* library = nullptr;
        YandexSource::Catalog* catalog = nullptr;
        LocalSource::Catalog* local = nullptr;
        Core::SourceRegistry* sources = nullptr;
        Core::LyricsFinder* lyrics = nullptr;
        YandexSource::Source* yandex = nullptr;
        Settings* settings = nullptr;
        WindowSet* windows = nullptr;
        Account* account = nullptr;
        JamBridge* jam = nullptr;
        TelemetryBridge* telemetry = nullptr;
        const Translations* translations = nullptr;
        Ui::MainWindow* mainWindow = nullptr;
        Ui::PlaylistWindow* playlistWindow = nullptr;
    };

    explicit MainMenu(const Parts& parts, QObject* parent = nullptr);

    void showMain(QPoint globalPosition, const Core::Track* track);
    void showSources(QPoint globalPosition);

Q_SIGNALS:
    void skinChosen(const QString& path);
    void languageChosen(App::Language language);
    void visModeChosen();
    void quitChosen();

private:
    void installShortcuts(QWidget* widget);
    void addLocalActions(QMenu* menu);
    void openLocalFiles();
    void openLocalFolder();
    QString localStartFolder() const;
    void loadSkinFile();
    void askForLrclib();
    void addWindowActions(QMenu* menu);
    void addModes(QMenu* menu);
    void addLooks(QMenu* menu);

    Parts parts;
};

}  // namespace App
