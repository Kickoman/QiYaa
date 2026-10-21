#pragma once

#include "app/translations.h"
#include "audio/equalizer.h"
#include "core/track_navigator.h"
#include "yandex/file_info.h"

#include <QDate>
#include <QPoint>
#include <QSettings>
#include <QSize>
#include <QString>
#include <QStringList>

#include <optional>

namespace App {

struct WindowState {
    std::optional<QPoint> pos;
    bool visible = false;
    bool shaded = false;
    QSize steps;
};

struct MainWindowState {
    int volume = 75;
    int balance = 0;
    int visMode = 0;
    bool remainingTime = false;
};

struct MilkdropSettings {
    bool shuffle = true;
    bool locked = false;
    int seconds = 30;
    QString preset;
    QStringList black;
};

struct JamSettings {
    QString server;
    bool waveFeedback = true;
    bool shareAudio = false;
};

class Settings {
public:
    explicit Settings(const QString& fileName);

    QString directory() const;
    bool wasEmpty() const { return empty; }

    std::optional<Language> language() const;
    void setLanguage(Language language);
    QString skin() const;
    void setSkin(const QString& path);
    double scale() const;
    void setScale(double scale);
    bool alwaysOnTop() const;
    void setAlwaysOnTop(bool on);

    Core::ShuffleAlgorithm shuffleAlgorithm() const;
    void setShuffleAlgorithm(Core::ShuffleAlgorithm algorithm);
    Yandex::Quality quality() const;
    void setQuality(Yandex::Quality quality);
    QString lastLocalFolder() const;
    void setLastLocalFolder(const QString& folder);
    bool continueWithVibe() const;
    bool lyricsThirdParty() const;
    void setLyricsThirdParty(bool on);
    QString spotifyDeviceId() const;
    void setSpotifyDeviceId(const QString& id);

    MainWindowState mainWindow() const;
    void setMainWindow(const MainWindowState& state);
    Audio::EqSettings equalizer() const;
    void setEqualizer(const Audio::EqSettings& eq);
    bool equalizerAuto() const;
    void setEqualizerAuto(bool on);

    WindowState window(const QString& group, bool visibleByDefault, QSize stepsByDefault = {}) const;
    void setWindowPos(const QString& group, QPoint pos);
    void setWindowVisible(const QString& group, bool on);
    void setWindowShaded(const QString& group, bool on);
    void setWindowSteps(const QString& group, QSize steps);

    MilkdropSettings milkdrop() const;
    void setMilkdrop(const MilkdropSettings& milkdrop);

    static QString DefaultJamServer();
    JamSettings jam() const;
    void setJam(const JamSettings& jam);

    bool telemetryEnabled() const;
    void setTelemetryEnabled(bool on);
    bool hasFirstStart() const;
    void setFirstStart(QDate date);
    QString machine() const;
    void setMachine(const QString& id);

private:
    QSettings store;
    bool empty;
};

}  // namespace App
