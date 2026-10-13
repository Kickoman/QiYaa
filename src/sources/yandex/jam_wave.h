#pragma once

#include "core/jam_mode.h"

#include <QHash>
#include <QString>

#include <memory>

namespace Yandex {
class Library;
struct WaveBatch;
}  // namespace Yandex

namespace YandexSource {

class JamWave : public Core::JamWave, public std::enable_shared_from_this<JamWave> {
public:
    JamWave(Yandex::Library* library, bool feedback, TRecent recent);

    bool start(const QStringList& seeds, int seedsVersion, const Core::Track* current, TDone done) override;
    void more(TDone done) override;
    void trackEvent(Core::Player::TrackEvent event, const Core::Track& track, double playedSeconds) override;
    bool hasSession() const override { return !session.isEmpty(); }
    int seedsVersion() const override { return sessionSeedsVersion; }
    void endSession() override;

private:
    struct Origin {
        QString session;
        QString station;
        QString batch;
    };

    void addBatch(const Yandex::WaveBatch& batch);

    Yandex::Library* yandexLibrary;
    bool sendsFeedback;
    TRecent recentTracks;
    QString session;
    QString station;
    int sessionSeedsVersion = -1;
    int sessionNumber = 0;
    QHash<QString, Origin> originOfTrack;
};

Core::TJamWaveFactory JamWaves(Yandex::Library* library);

}  // namespace YandexSource
