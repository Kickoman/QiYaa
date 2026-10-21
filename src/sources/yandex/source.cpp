#include "sources/yandex/source.h"

#include "sources/yandex/tracks.h"
#include "yandex/download.h"

#include <QTimer>
#include <QUuid>

#include <memory>
#include <optional>
#include <utility>

namespace YandexSource {

namespace {
class FileDownload : public Core::TrackDownload {
public:
    FileDownload(Yandex::ApiClient* client, QUrl file)
        : api(client)
        , link(std::move(file)) { }

    void start() override {
        if (download) {
            return;
        }
        download = api->download(link);
        connect(download.get(), &Yandex::Download::received, this, &Core::TrackDownload::received);
        connect(download.get(), &Yandex::Download::finished, this, [this](const Yandex::RequestError& error) {
            if (error.isError()) {
                Q_EMIT finished(Core::TrackFailure{FailureKindOf(error), error.text});
            } else {
                Q_EMIT finished(std::nullopt);
            }
        });
    }

private:
    Yandex::ApiClient* api;
    QUrl link;
    std::unique_ptr<Yandex::Download> download;
};
}  // namespace

Source::Source(Yandex::Library* library, QObject* parent)
    : Core::TrackSource(parent)
    , yandexLibrary(library) { }

QUrl Source::coverUrl(const Core::Track& track, int pixels) const {
    const std::optional<Yandex::Track> yandexTrack = YandexTrackOf(track);
    return yandexTrack ? yandexTrack->coverUrl(pixels) : QUrl();
}

QUrl Source::webPage(const Core::Track& track) const {
    const std::optional<Yandex::Track> yandexTrack = YandexTrackOf(track);
    return yandexTrack ? yandexTrack->webUrl() : QUrl();
}

void Source::open(const Core::Track& track, TOpened done) {
    Yandex::ApiClient* api = yandexLibrary->api();
    api->resolveTrackUrl(
        track.key.id,
        fileQuality,
        [api, done = std::move(done)](const Yandex::ResolvedUrl& link, const Yandex::RequestError& error) {
            if (error.isError()) {
                return done(Core::TrackFailure{FailureKindOf(error), error.text});
            }
            done(
                Core::OpenedTrack{std::make_unique<FileDownload>(api, link.url), link.url, link.bitrateKbps, link.codec}
            );
        }
    );
}

void Source::trackEvent(Core::TrackEvent event, const Core::Track& track, double) {
    if (event != Core::TrackEvent::Started) {
        return;
    }
    yandexLibrary->api()->reportPlayStarted(
        yandexLibrary->account(), track.key.id, track.durationMs, QUuid::createUuid().toString(QUuid::WithoutBraces)
    );
}

void Source::lyrics(const Core::Track& track, Core::TLyricsDone done) {
    Yandex::ApiClient* api = yandexLibrary->api();
    if (!api->hasToken()) {
        QTimer::singleShot(0, this, [done = std::move(done)] {
            done(Core::LyricsAnswer{.status = Core::LyricsAnswer::Status::NoAccount});
        });
        return;
    }
    struct Parts {
        std::optional<Core::LyricsAnswer> synced;
        std::optional<Core::LyricsAnswer> plain;
        Core::TLyricsDone done;
    };
    auto parts = std::make_shared<Parts>(Parts{.done = std::move(done)});
    const auto finish = [parts] {
        if (!parts->synced || !parts->plain) {
            return;
        }
        const bool synced = parts->synced->status == Core::LyricsAnswer::Status::Found;
        parts->done(synced ? *parts->synced : *parts->plain);
    };
    const QString id = CatalogId(track.key);
    askSynced(id, [parts, finish](Core::LyricsAnswer answer) {
        parts->synced = std::move(answer);
        finish();
    });
    api->lyricsText(id, [parts, finish](const std::optional<QString>& text, const Yandex::RequestError& error) {
        Core::LyricsAnswer answer;
        if (error.isError()) {
            const bool mayPass = error.kind == Yandex::RequestError::Kind::Network || error.httpStatus >= 500;
            answer.status = mayPass ? Core::LyricsAnswer::Status::Failed : Core::LyricsAnswer::Status::Missing;
        } else if (const std::optional<Core::Lyrics> lyrics = text ? Core::PlainLyrics(*text) : std::nullopt) {
            answer = Core::LyricsAnswer{.status = Core::LyricsAnswer::Status::Found, .lyrics = *lyrics};
        }
        parts->plain = std::move(answer);
        finish();
    });
}

void Source::askSynced(const QString&, Core::TLyricsDone done) {
    QTimer::singleShot(0, this, [done = std::move(done)] { done(Core::LyricsAnswer{}); });
}

void Source::setQuality(Yandex::Quality quality) {
    if (quality == fileQuality) {
        return;
    }
    fileQuality = quality;
    Q_EMIT filesChanged();
}

Core::FailureKind FailureKindOf(const Yandex::RequestError& error) {
    switch (error.kind) {
        case Yandex::RequestError::Kind::Network: return Core::FailureKind::Network;
        case Yandex::RequestError::Kind::Http:
            return error.httpStatus == 401 || error.httpStatus == 403
                ? Core::FailureKind::Auth
                : Core::FailureKind::Track;
        case Yandex::RequestError::Kind::None:
        case Yandex::RequestError::Kind::Content: break;
    }
    return Core::FailureKind::Track;
}

}  // namespace YandexSource
