#pragma once

#include "core/continuation.h"
#include "core/track.h"

#include <QPointer>
#include <QString>
#include <QStringList>

#include <memory>

namespace SpotifySource {

class Library;

class Autoplay : public Core::Continuation, public std::enable_shared_from_this<Autoplay> {
public:
    static constexpr int kRecentTracks = 50;

    static std::shared_ptr<Autoplay> Of(Library* library, const QString& contextUri);
    static std::shared_ptr<Autoplay> Station(Library* library, const QString& trackUri, const QString& nextPage);

    bool more(const Core::ContinuationRequest& request, TDone done) override;
    void trackEvent(Core::TrackEvent, const Core::Track&, double) override { }

    static QStringList RecentUris(const QList<Core::Track>& history);

private:
    Autoplay(Library* library, QString contextUri, QString stationTrack, QString nextPage);

    QPointer<Library> library;
    QString context;
    QString stationTrack;
    QString next;
};

}  // namespace SpotifySource
