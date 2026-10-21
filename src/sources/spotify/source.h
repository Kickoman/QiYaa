#pragma once

#include "core/track_source.h"

class QNetworkAccessManager;

namespace SpotifyClient {
class Session;
class Web;
}  // namespace SpotifyClient

namespace SpotifySource {

class Source : public Core::TrackSource {
    Q_OBJECT
public:
    static constexpr int kStandardKbps = 320;
    static constexpr int kEconomyKbps = 96;

    Source(
        SpotifyClient::Web* web,
        SpotifyClient::Session* session,
        QNetworkAccessManager* network,
        QObject* parent = nullptr
    );

    QUrl coverUrl(const Core::Track& track, int pixels) const override;
    QUrl webPage(const Core::Track& track) const override;
    void open(const Core::Track& track, TOpened done) override;
    void trackEvent(Core::TrackEvent event, const Core::Track& track, double playedSeconds) override;
    void lyrics(const Core::Track& track, Core::TLyricsDone done) override;

    void setEconomy(bool economy);
    int preferredKbps() const { return kbps; }

private:
    SpotifyClient::Web* web;
    SpotifyClient::Session* session;
    QNetworkAccessManager* network;
    int kbps = kStandardKbps;
};

}  // namespace SpotifySource
