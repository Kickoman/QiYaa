#include "app/settings.h"

#include <QChar>
#include <QFileInfo>
#include <QLatin1String>

#include <algorithm>

namespace App {

namespace {

QString Key(const QString& group, const char* field) {
    return group + QLatin1Char('/') + QLatin1String(field);
}

}  // namespace

Settings::Settings(const QString& fileName)
    : store(fileName, QSettings::IniFormat)
    , empty(store.allKeys().isEmpty()) { }

QString Settings::directory() const {
    return QFileInfo(store.fileName()).absolutePath();
}

std::optional<Language> Settings::language() const {
    return LanguageFromCode(store.value(QStringLiteral("language")).toString());
}

void Settings::setLanguage(Language language) {
    store.setValue(QStringLiteral("language"), LanguageCode(language));
}

QString Settings::skin() const {
    return store.value(QStringLiteral("skin")).toString();
}

void Settings::setSkin(const QString& path) {
    store.setValue(QStringLiteral("skin"), path);
}

double Settings::scale() const {
    return store.value(QStringLiteral("scale"), 1.0).toDouble();
}

void Settings::setScale(double scale) {
    store.setValue(QStringLiteral("scale"), scale);
}

bool Settings::alwaysOnTop() const {
    return store.value(QStringLiteral("alwaysOnTop"), false).toBool();
}

void Settings::setAlwaysOnTop(bool on) {
    store.setValue(QStringLiteral("alwaysOnTop"), on);
}

Core::ShuffleAlgorithm Settings::shuffleAlgorithm() const {
    const bool random = store.value(QStringLiteral("shuffle/algorithm")).toString() == QLatin1String("random");
    return random ? Core::ShuffleAlgorithm::Random : Core::ShuffleAlgorithm::WithoutRepeats;
}

void Settings::setShuffleAlgorithm(Core::ShuffleAlgorithm algorithm) {
    const bool random = algorithm == Core::ShuffleAlgorithm::Random;
    store.setValue(
        QStringLiteral("shuffle/algorithm"), random ? QStringLiteral("random") : QStringLiteral("without-repeats")
    );
}

Yandex::Quality Settings::quality() const {
    const QString key = store.value(QStringLiteral("playback/quality")).toString();
    return Yandex::QualityFromKey(key).value_or(Yandex::Quality::Standard);
}

void Settings::setQuality(Yandex::Quality quality) {
    store.setValue(QStringLiteral("playback/quality"), Yandex::QualityKey(quality));
}

QString Settings::lastLocalFolder() const {
    return store.value(QStringLiteral("local/lastFolder")).toString();
}

void Settings::setLastLocalFolder(const QString& folder) {
    if (folder != lastLocalFolder()) {
        store.setValue(QStringLiteral("local/lastFolder"), folder);
    }
}

QString Settings::spotifyDeviceId() const {
    return store.value(QStringLiteral("spotify/deviceId")).toString();
}

void Settings::setSpotifyDeviceId(const QString& id) {
    store.setValue(QStringLiteral("spotify/deviceId"), id);
}

bool Settings::continueWithVibe() const {
    return store.value(QStringLiteral("playback/continueWithVibe"), true).toBool();
}

bool Settings::lyricsThirdParty() const {
    return store.value(QStringLiteral("lyrics/thirdParty"), false).toBool();
}

void Settings::setLyricsThirdParty(bool on) {
    store.setValue(QStringLiteral("lyrics/thirdParty"), on);
}

MainWindowState Settings::mainWindow() const {
    MainWindowState state;
    state.volume = store.value(QStringLiteral("volume"), state.volume).toInt();
    state.balance = store.value(QStringLiteral("balance"), state.balance).toInt();
    state.visMode = std::clamp(store.value(QStringLiteral("vis/mode"), state.visMode).toInt(), 0, 2);
    state.remainingTime = store.value(QStringLiteral("time/remaining"), state.remainingTime).toBool();
    return state;
}

void Settings::setMainWindow(const MainWindowState& state) {
    store.setValue(QStringLiteral("volume"), state.volume);
    store.setValue(QStringLiteral("balance"), state.balance);
    store.setValue(QStringLiteral("vis/mode"), state.visMode);
    store.setValue(QStringLiteral("time/remaining"), state.remainingTime);
}

Audio::EqSettings Settings::equalizer() const {
    Audio::EqSettings eq;
    eq.enabled = store.value(QStringLiteral("equalizer/enabled"), eq.enabled).toBool();
    eq.preampDb = store.value(QStringLiteral("equalizer/preamp"), eq.preampDb).toDouble();
    const QStringList bands = store.value(QStringLiteral("equalizer/bands")).toStringList();
    for (int i = 0; i < Audio::kEqBands && i < bands.size(); ++i) {
        eq.bandsDb[i] = bands[i].toDouble();
    }
    return eq;
}

void Settings::setEqualizer(const Audio::EqSettings& eq) {
    store.setValue(QStringLiteral("equalizer/enabled"), eq.enabled);
    store.setValue(QStringLiteral("equalizer/preamp"), eq.preampDb);
    QStringList bands;
    for (double bandDb : eq.bandsDb) {
        bands << QString::number(bandDb, 'f', 1);
    }
    store.setValue(QStringLiteral("equalizer/bands"), bands);
}

bool Settings::equalizerAuto() const {
    return store.value(QStringLiteral("equalizer/auto"), false).toBool();
}

void Settings::setEqualizerAuto(bool on) {
    store.setValue(QStringLiteral("equalizer/auto"), on);
}

WindowState Settings::window(const QString& group, bool visibleByDefault, QSize stepsByDefault) const {
    WindowState state;
    const QString pos = Key(group, "pos");
    if (store.contains(pos)) {
        state.pos = store.value(pos).toPoint();
    }
    state.visible = store.value(Key(group, "visible"), visibleByDefault).toBool();
    state.shaded = store.value(Key(group, "shaded"), false).toBool();
    state.steps = store.value(Key(group, "steps"), stepsByDefault).toSize();
    return state;
}

void Settings::setWindowPos(const QString& group, QPoint pos) {
    store.setValue(Key(group, "pos"), pos);
}

void Settings::setWindowVisible(const QString& group, bool on) {
    store.setValue(Key(group, "visible"), on);
}

void Settings::setWindowShaded(const QString& group, bool on) {
    store.setValue(Key(group, "shaded"), on);
}

void Settings::setWindowSteps(const QString& group, QSize steps) {
    store.setValue(Key(group, "steps"), steps);
}

MilkdropSettings Settings::milkdrop() const {
    MilkdropSettings milkdrop;
    milkdrop.shuffle = store.value(QStringLiteral("milkdrop/shuffle"), milkdrop.shuffle).toBool();
    milkdrop.locked = store.value(QStringLiteral("milkdrop/locked"), milkdrop.locked).toBool();
    milkdrop.seconds = store.value(QStringLiteral("milkdrop/seconds"), milkdrop.seconds).toInt();
    milkdrop.preset = store.value(QStringLiteral("milkdrop/preset")).toString();
    milkdrop.black = store.value(QStringLiteral("milkdrop/black")).toStringList();
    return milkdrop;
}

void Settings::setMilkdrop(const MilkdropSettings& milkdrop) {
    store.setValue(QStringLiteral("milkdrop/shuffle"), milkdrop.shuffle);
    store.setValue(QStringLiteral("milkdrop/locked"), milkdrop.locked);
    store.setValue(QStringLiteral("milkdrop/seconds"), milkdrop.seconds);
    store.setValue(QStringLiteral("milkdrop/preset"), milkdrop.preset);
    store.setValue(QStringLiteral("milkdrop/black"), milkdrop.black);
}

QString Settings::DefaultJamServer() {
    return QStringLiteral(QIYAA_JAM_URL);
}

JamSettings Settings::jam() const {
    JamSettings jam;
    jam.server = store.value(QStringLiteral("jam/server"), DefaultJamServer()).toString().trimmed();
    jam.waveFeedback = store.value(QStringLiteral("jam/waveFeedback"), jam.waveFeedback).toBool();
    jam.shareAudio = store.value(QStringLiteral("jam/shareAudio"), jam.shareAudio).toBool();
    return jam;
}

void Settings::setJam(const JamSettings& jam) {
    store.setValue(QStringLiteral("jam/server"), jam.server);
    store.setValue(QStringLiteral("jam/waveFeedback"), jam.waveFeedback);
    store.setValue(QStringLiteral("jam/shareAudio"), jam.shareAudio);
}

bool Settings::telemetryEnabled() const {
    return store.value(QStringLiteral("telemetry/enabled"), true).toBool();
}

void Settings::setTelemetryEnabled(bool on) {
    store.setValue(QStringLiteral("telemetry/enabled"), on);
}

bool Settings::hasFirstStart() const {
    return store.contains(QStringLiteral("telemetry/firstStart"));
}

void Settings::setFirstStart(QDate date) {
    store.setValue(QStringLiteral("telemetry/firstStart"), date.toString(Qt::ISODate));
    store.sync();
}

QString Settings::machine() const {
    return store.value(QStringLiteral("telemetry/machine")).toString();
}

void Settings::setMachine(const QString& id) {
    store.setValue(QStringLiteral("telemetry/machine"), id);
}

}  // namespace App
